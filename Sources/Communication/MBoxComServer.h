#pragma once

#include "MBoxComAPI.h"

#include <juce_core/juce_core.h>

#include <functional>
#include <map>
#include <memory>
#include <vector>

class MBoxApplication;

/** TCP server exposing MBoxApplication to remote clients.

    Wire format: each message is a raw Message struct (sizeof (Message) bytes).
    - Request  (client -> server): methodName + parameters. The method runs on the message thread.
    - Response (server -> client): same requestId and methodName, results filled in.
    - Event    (server -> all clients): sent by sendEvent().

    String-array results are joined with '\n'. Void methods reply "ok"; failures reply "error: ...". */
class MBoxComServer final
{
public:
    static constexpr int defaultPort = 5050;

    explicit MBoxComServer (MBoxApplication& application, int port = defaultPort);
    ~MBoxComServer();

    /** Broadcasts an Event to every connected client. Safe to call from any thread. */
    void sendEvent (const juce::String& eventName, const juce::String& information);

private:
    class Connection;
    class Listener;

    using Handler = std::function<juce::String (const Message&)>;

    void registerMethods();
    void addConnection (std::unique_ptr<juce::StreamingSocket> socket);
    void handleRequest (std::shared_ptr<Connection> connection, const Message& request);
    Message processRequest (const Message& request) const;

    MBoxApplication& m_application;
    std::map<juce::String, Handler> m_handlers;

    juce::CriticalSection m_connectionsLock;
    std::vector<std::shared_ptr<Connection>> m_connections;

    std::unique_ptr<Listener> m_listener;

    JUCE_DECLARE_WEAK_REFERENCEABLE (MBoxComServer)
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MBoxComServer)
};
