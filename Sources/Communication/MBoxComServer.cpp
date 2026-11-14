#include "MBoxComServer.h"

#include "../Model/MBoxApplication.h"

#include <juce_events/juce_events.h>

#include <cstring>

namespace
{
    juce::String readField (const char* field, std::size_t length)
    {
        return juce::String::fromUTF8 (field, (int) strnlen (field, length));
    }

    void writeField (char* field, std::size_t length, const juce::String& value)
    {
        std::memset (field, 0, length);
        value.copyToUTF8 (field, length); // truncates and null-terminates
    }
}

//==============================================================================
/** One connected client: a thread reading Messages from its socket. */
class MBoxComServer::Connection final : public juce::Thread,
                                        public std::enable_shared_from_this<Connection>
{
public:
    Connection (MBoxComServer& server, std::unique_ptr<juce::StreamingSocket> socket)
        : juce::Thread ("MBoxComConnection"), m_server (server), m_socket (std::move (socket)) {}

    ~Connection() override
    {
        signalThreadShouldExit();
        m_socket->close(); // unblocks read()
        stopThread (2000);
    }

    bool isConnected() const { return m_connected; }

    bool send (const Message& message)
    {
        const juce::ScopedLock sl (m_writeLock);
        return m_socket->write (&message, (int) sizeof (Message)) == (int) sizeof (Message);
    }

    void run() override
    {
        juce::Logger::writeToLog ("MBoxComServer: client connected from " + m_socket->getHostName());

        while (! threadShouldExit())
        {
            Message request {};

            if (m_socket->read (&request, (int) sizeof (Message), true) != (int) sizeof (Message))
                break;

            if (request.type == MessageType::Request)
                m_server.handleRequest (shared_from_this(), request);
        }

        m_connected = false;
        juce::Logger::writeToLog ("MBoxComServer: client disconnected");
    }

private:
    MBoxComServer& m_server;
    std::unique_ptr<juce::StreamingSocket> m_socket;
    juce::CriticalSection m_writeLock;
    std::atomic<bool> m_connected { true };
};

//==============================================================================
/** Accepts incoming connections on the listening socket. */
class MBoxComServer::Listener final : public juce::Thread
{
public:
    Listener (MBoxComServer& server, int port) : juce::Thread ("MBoxComListener"), m_server (server), m_port (port) {}

    ~Listener() override
    {
        signalThreadShouldExit();
        m_socket.close(); // unblocks waitForNextConnection()
        stopThread (2000);
    }

    bool listen() { return m_socket.createListener (m_port); }

    void run() override
    {
        while (! threadShouldExit())
        {
            std::unique_ptr<juce::StreamingSocket> client (m_socket.waitForNextConnection());

            if (client == nullptr)
            {
                if (! m_socket.isConnected())
                    break;

                continue;
            }

            m_server.addConnection (std::move (client));
        }
    }

private:
    MBoxComServer& m_server;
    const int m_port;
    juce::StreamingSocket m_socket;
};

//==============================================================================
MBoxComServer::MBoxComServer (MBoxApplication& application, int port)
    : m_application (application)
{
    registerMethods();

    m_listener = std::make_unique<Listener> (*this, port);

    if (! m_listener->listen())
    {
        juce::Logger::writeToLog ("MBoxComServer: unable to listen on port " + juce::String (port));
        return;
    }

    juce::Logger::writeToLog ("MBoxComServer: listening on port " + juce::String (port));
    m_listener->startThread();
}

MBoxComServer::~MBoxComServer()
{
    m_listener.reset(); // no new connections

    std::vector<std::shared_ptr<Connection>> connections;
    {
        const juce::ScopedLock sl (m_connectionsLock);
        connections.swap (m_connections);
    }
    connections.clear(); // closes sockets and joins threads outside the lock
}

void MBoxComServer::registerMethods()
{
    m_handlers["selectSong"] = [this] (const Message& m)
    {
        m_application.selectSong (readField (m.parameters[0], fieldLength), readField (m.parameters[1], fieldLength));
        return juce::String ("ok");
    };

    m_handlers["play"]  = [this] (const Message&) { m_application.play();  return juce::String ("ok"); };
    m_handlers["pause"] = [this] (const Message&) { m_application.pause(); return juce::String ("ok"); };
    m_handlers["stop"]  = [this] (const Message&) { m_application.stop();  return juce::String ("ok"); };

    m_handlers["quit"] = [this] (const Message&)
    {
        m_application.stop();
        // quit() only posts a quit message, so the "ok" response is sent before shutdown starts.
        juce::JUCEApplicationBase::quit();
        return juce::String ("ok");
    };

    m_handlers["getPlaylistNames"] = [this] (const Message&)
    {
        return m_application.getPlaylistNames().joinIntoString ("\n");
    };

    m_handlers["getSongNames"] = [this] (const Message& m)
    {
        return m_application.getSongNames (readField (m.parameters[0], fieldLength)).joinIntoString ("\n");
    };
}

void MBoxComServer::addConnection (std::unique_ptr<juce::StreamingSocket> socket)
{
    auto connection = std::make_shared<Connection> (*this, std::move (socket));

    std::vector<std::shared_ptr<Connection>> disconnected;
    {
        const juce::ScopedLock sl (m_connectionsLock);

        // Drop clients that have gone away (destroyed outside the lock below).
        for (auto it = m_connections.begin(); it != m_connections.end();)
        {
            if (! (*it)->isConnected())
            {
                disconnected.push_back (std::move (*it));
                it = m_connections.erase (it);
            }
            else
            {
                ++it;
            }
        }

        m_connections.push_back (connection);
    }

    connection->startThread();
}

void MBoxComServer::handleRequest (std::shared_ptr<Connection> connection, const Message& request)
{
    // MBoxApplication isn't thread-safe: run the call on the message thread, then reply from there.
    juce::MessageManager::callAsync ([weakServer = juce::WeakReference<MBoxComServer> (this),
                                      connection = std::move (connection),
                                      request]
    {
        if (auto* server = weakServer.get())
            connection->send (server->processRequest (request));
    });
}

Message MBoxComServer::processRequest (const Message& request) const
{
    Message response {};
    response.requestId = request.requestId;
    response.type = MessageType::Response;
    std::memcpy (response.methodName, request.methodName, sizeof (response.methodName));

    const auto methodName = readField (request.methodName, fieldLength);
    const auto it = m_handlers.find (methodName);

    const auto results = it != m_handlers.end() ? it->second (request)
                                                : "error: unknown method '" + methodName + "'";

    writeField (response.results, resultsLength, results);
    return response;
}

void MBoxComServer::sendEvent (const juce::String& eventName, const juce::String& information)
{
    Message event {};
    event.requestId = 0;
    event.type = MessageType::Event;
    writeField (event.methodName, fieldLength, eventName);
    writeField (event.results, resultsLength, information);

    std::vector<std::shared_ptr<Connection>> connections;
    {
        const juce::ScopedLock sl (m_connectionsLock);
        connections = m_connections;
    }

    for (const auto& connection : connections)
        if (connection->isConnected())
            connection->send (event);
}
