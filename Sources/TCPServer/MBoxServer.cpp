#include "MBoxServer.h"

namespace
{
    juce::String toCompactJson (const juce::var& value)
    {
        return juce::JSON::toString (value, juce::JSON::FormatOptions {}.withSpacing (juce::JSON::Spacing::none));
    }

    juce::var makeObject()  { return juce::var (new juce::DynamicObject()); }

    void set (juce::var& object, const juce::Identifier& name, const juce::var& value)
    {
        object.getDynamicObject()->setProperty (name, value);
    }

    // The outcome of a dispatched request: a result, or an error code and message
    struct Outcome
    {
        juce::var m_result;
        int m_code { 0 };
        juce::String m_message;

        static Outcome ok (juce::var result = makeObject())  { return { std::move (result), 0, {} }; }
        static Outcome error (int code, const juce::String& message)  { return { {}, code, message }; }

        static Outcome from (const MBoxResult& result)
        {
            return result.wasOk() ? ok() : error (MBoxComAPI::rejected, result.m_message);
        }
    };

    //==========================================================================
    // Typed parameter readers. Each returns false when the parameter is missing or has the
    // wrong JSON type.
    bool getInt (const juce::var& params, const char* name, int& value)
    {
        const auto& item = params[name];

        if (! (item.isInt() || item.isInt64() || (item.isDouble() && std::floor ((double) item) == (double) item)))
            return false;

        value = (int) item;
        return true;
    }

    bool getDouble (const juce::var& params, const char* name, double& value)
    {
        const auto& item = params[name];

        if (! (item.isInt() || item.isInt64() || item.isDouble()))
            return false;

        value = (double) item;
        return true;
    }

    bool getBool (const juce::var& params, const char* name, bool& value)
    {
        const auto& item = params[name];

        if (! item.isBool())
            return false;

        value = (bool) item;
        return true;
    }

    bool getString (const juce::var& params, const char* name, std::string& value)
    {
        const auto& item = params[name];

        if (! item.isString())
            return false;

        value = item.toString().toStdString();
        return true;
    }

    // target: {"slot": {"bank": b, "slot": s}}, {"insert": {"bus": b, "track": t}} or {"master": {"bus": b}}
    bool getTarget (const juce::var& params, MBoxEffectTarget& target)
    {
        const auto& json = params["target"];

        if (json.hasProperty ("slot"))
        {
            target.m_kind = MBoxEffectTarget::Kind::Slot;
            return getInt (json["slot"], "bank", target.m_bank) && getInt (json["slot"], "slot", target.m_slot);
        }

        if (json.hasProperty ("insert"))
        {
            target.m_kind = MBoxEffectTarget::Kind::Insert;
            return getInt (json["insert"], "bus", target.m_bus) && getInt (json["insert"], "track", target.m_track);
        }

        if (json.hasProperty ("master"))
        {
            target.m_kind = MBoxEffectTarget::Kind::Master;
            return getInt (json["master"], "bus", target.m_bus);
        }

        return false;
    }

    juce::var namesToJson (const std::vector<std::string>& names)
    {
        juce::Array<juce::var> list;

        for (const auto& name : names)
            list.add (juce::String (name));

        return list;
    }
}

//==============================================================================
// One connected client: a thread reading lines from its socket
//==============================================================================
class MBoxServer::Client final : private juce::Thread
{
public:
    Client (juce::WeakReference<MBoxServer> server, std::unique_ptr<juce::StreamingSocket> socket, int id)
        : juce::Thread ("MBox TCP client " + juce::String (id)),
          m_server (std::move (server)), m_socket (std::move (socket)), m_id (id)
    {
        startThread();
    }

    ~Client() override
    {
        signalThreadShouldExit();
        m_socket->close();
        stopThread (2000);
    }

    int getId() const noexcept     { return m_id; }
    bool isDead() const noexcept   { return m_dead; }

    // Message thread
    bool m_helloDone { false };

    // Writes one line. Called from the message thread.
    void send (const juce::String& line)
    {
        const juce::ScopedLock lock (m_writeLock);
        const auto data = line + "\n";

        if (m_socket->write (data.toRawUTF8(), (int) data.getNumBytesAsUTF8()) < 0)
            m_dead = true;
    }

private:
    // Reads bytes, splits them into lines and passes each line to the message thread.
    // Disconnects a client that sends nothing for clientTimeoutMs or a line that is too long.
    void run() override
    {
        juce::MemoryOutputStream pending;
        char buffer[4096];
        auto lastActivity = juce::Time::getMillisecondCounter();

        while (! threadShouldExit())
        {
            const auto ready = m_socket->waitUntilReady (true, 200);

            if (ready < 0)
                break;

            if (ready == 0)
            {
                if (juce::Time::getMillisecondCounter() - lastActivity > (juce::uint32) MBoxComAPI::clientTimeoutMs)
                    break;

                continue;
            }

            const auto numRead = m_socket->read (buffer, (int) sizeof (buffer), false);

            if (numRead <= 0)
                break;

            lastActivity = juce::Time::getMillisecondCounter();

            for (int i = 0; i < numRead; ++i)
            {
                if (buffer[i] != '\n')
                {
                    pending.writeByte (buffer[i]);
                    continue;
                }

                auto line = juce::String::fromUTF8 ((const char*) pending.getData(), (int) pending.getDataSize()).trimEnd();
                pending.reset();

                if (line.isNotEmpty())
                    postLine (line);
            }

            if ((int) pending.getDataSize() > MBoxComAPI::maxMessageBytes)
            {
                send ("{\"id\":null,\"error\":{\"code\":1,\"message\":\"Message too long\"}}");
                break;
            }
        }

        m_dead = true;
    }

    void postLine (const juce::String& line)
    {
        juce::MessageManager::callAsync ([server = m_server, id = m_id, line]
        {
            auto* s = server.get();

            if (s == nullptr)
                return;

            // Held while handling, since the request can broadcast events, which removes dead clients
            std::shared_ptr<Client> client;

            {
                const juce::ScopedLock lock (s->m_clientsLock);

                for (auto& candidate : s->m_clients)
                    if (candidate->getId() == id)
                        client = candidate;
            }

            if (client != nullptr && ! client->isDead())
                client->send (s->handleRequestLine (line, client->m_helloDone));
        });
    }

    juce::WeakReference<MBoxServer> m_server;
    std::unique_ptr<juce::StreamingSocket> m_socket;
    const int m_id;
    juce::CriticalSection m_writeLock;
    std::atomic<bool> m_dead { false };
};

//==============================================================================
// Waits for connections and creates a Client for each
//==============================================================================
class MBoxServer::Acceptor final : private juce::Thread
{
public:
    Acceptor (MBoxServer& server, std::unique_ptr<juce::StreamingSocket> listener)
        : juce::Thread ("MBox TCP acceptor"), m_server (server), m_weakServer (&server), m_listener (std::move (listener))
    {
        startThread();
    }

    ~Acceptor() override
    {
        signalThreadShouldExit();
        m_listener->close();  // unblocks waitForNextConnection
        stopThread (2000);
    }

private:
    void run() override
    {
        while (! threadShouldExit())
        {
            std::unique_ptr<juce::StreamingSocket> socket (m_listener->waitForNextConnection());

            if (socket == nullptr)
            {
                if (threadShouldExit() || ! m_listener->isConnected())
                    break;

                continue;
            }

            const juce::ScopedLock lock (m_server.m_clientsLock);
            const auto id = m_server.m_nextClientId++;
            m_server.m_clients.push_back (std::make_shared<Client> (m_weakServer, std::move (socket), id));
            juce::Logger::writeToLog ("MBoxServer: client " + juce::String (id) + " connected");
        }
    }

    MBoxServer& m_server;
    juce::WeakReference<MBoxServer> m_weakServer;
    std::unique_ptr<juce::StreamingSocket> m_listener;
};

//==============================================================================
// MBoxServer
//==============================================================================
MBoxServer::MBoxServer (MBoxCoreAudio& core)
    : m_core (core)
{
}

MBoxServer::~MBoxServer()
{
    stop();
    masterReference.clear();
}

juce::Result MBoxServer::start (int port)
{
    stop();

    auto listener = std::make_unique<juce::StreamingSocket>();

    if (! listener->createListener (port))
        return juce::Result::fail ("Can't listen on TCP port " + juce::String (port));

    m_port = listener->getBoundPort();
    m_acceptor = std::make_unique<Acceptor> (*this, std::move (listener));
    juce::Logger::writeToLog ("MBoxServer: listening on port " + juce::String (m_port));
    return juce::Result::ok();
}

void MBoxServer::stop()
{
    m_acceptor.reset();

    // Destroyed outside the lock: a client thread may be waiting for it in postLine's callback
    std::vector<std::shared_ptr<Client>> clients;

    {
        const juce::ScopedLock lock (m_clientsLock);
        clients.swap (m_clients);
    }

    clients.clear();
}

int MBoxServer::getNumClients() const
{
    const juce::ScopedLock lock (m_clientsLock);
    return (int) m_clients.size();
}

void MBoxServer::removeDeadClients()
{
    std::vector<std::shared_ptr<Client>> dead;

    {
        const juce::ScopedLock lock (m_clientsLock);

        for (auto it = m_clients.begin(); it != m_clients.end();)
        {
            if ((*it)->isDead())
            {
                juce::Logger::writeToLog ("MBoxServer: client " + juce::String ((*it)->getId()) + " disconnected");
                dead.push_back (std::move (*it));
                it = m_clients.erase (it);
            }
            else
            {
                ++it;
            }
        }
    }
}

void MBoxServer::broadcast (const MBoxEvent& event)
{
    removeDeadClients();

    juce::var data;
    juce::JSON::parse (juce::String (event.m_json), data);

    auto message = makeObject();
    set (message, "event", juce::String (event.m_name));
    set (message, "data", data);
    const auto line = toCompactJson (message);

    const juce::ScopedLock lock (m_clientsLock);

    for (auto& client : m_clients)
        if (client->m_helloDone && ! client->isDead())
            client->send (line);
}

//==============================================================================
// Maps each method onto the MBoxCoreAudio public API. API failures become "rejected"
// errors carrying the API's message.
//==============================================================================
namespace
{
    Outcome dispatch (MBoxCoreAudio& core, const juce::String& method, const juce::var& params)
    {
        namespace M = MBoxComAPI::Method;

        auto badParameters = [&] (const juce::String& expected)
        {
            return Outcome::error (MBoxComAPI::badParameters, method + " needs " + expected);
        };

        if (method == M::ping)
            return Outcome::ok();

        if (method == M::getState)
        {
            const auto info = core.getTransportInfo();
            auto result = makeObject();
            set (result, "state", juce::String (info.m_state));
            set (result, "playlist", juce::String (info.m_playlist));
            set (result, "song", juce::String (info.m_song));
            set (result, "position", juce::String (info.m_position));
            set (result, "bpm", info.m_bpm);
            set (result, "numerator", info.m_numerator);
            set (result, "denominator", info.m_denominator);
            set (result, "loading", info.m_loading);
            set (result, "loadingProgress", info.m_loadingProgress);
            return Outcome::ok (result);
        }

        if (method == M::getPlaylists || method == M::getPlaylistNames)
        {
            auto result = makeObject();
            set (result, "playlists", namesToJson (core.getPlaylistNames()));
            return Outcome::ok (result);
        }

        if (method == M::getSongs)
        {
            std::string playlist;

            if (! getString (params, "playlist", playlist))
                return badParameters ("playlist");

            auto result = makeObject();
            set (result, "songs", namesToJson (core.getSongNames (playlist)));
            return Outcome::ok (result);
        }

        if (method == M::selectSong)
        {
            std::string playlist, song;

            if (! getString (params, "playlist", playlist) || ! getString (params, "song", song))
                return badParameters ("playlist and song");

            return Outcome::from (core.selectSong (playlist, song));
        }

        if (method == M::renamePlaylist)
        {
            std::string playlist, newName;

            if (! getString (params, "playlist", playlist) || ! getString (params, "newName", newName))
                return badParameters ("playlist and newName");

            return Outcome::from (core.renamePlaylist (playlist, newName));
        }

        if (method == M::renameSong)
        {
            std::string playlist, song, newName;

            if (! getString (params, "playlist", playlist) || ! getString (params, "song", song)
                || ! getString (params, "newName", newName))
                return badParameters ("playlist, song and newName");

            return Outcome::from (core.renameSong (playlist, song, newName));
        }

        if (method == M::playFrom)
        {
            std::string position;

            if (! getString (params, "position", position))
                return badParameters ("position");

            return Outcome::from (core.playFrom (position));
        }

        if (method == M::setMetronomeInMix)
        {
            int bus = 0;
            bool on = true;

            if (! getInt (params, "bus", bus) || ! getBool (params, "on", on))
                return badParameters ("bus and on");

            return Outcome::from (core.setMetronomeInMix (bus, on));
        }

        if (method == M::play)   return Outcome::from (core.play());
        if (method == M::pause)  return Outcome::from (core.pause());
        if (method == M::stop)   return Outcome::from (core.stop());

        if (method == M::getMix)
        {
            int bus = 0;

            if (! getInt (params, "bus", bus))
                return badParameters ("bus");

            juce::var tracks;
            juce::JSON::parse (juce::String (core.getMixJson (bus)), tracks);
            auto result = makeObject();
            set (result, "tracks", tracks);
            return Outcome::ok (result);
        }

        if (method == M::setTrackVolume || method == M::setTrackBalance || method == M::setTrackMute || method == M::setTrackSolo)
        {
            int bus = 0, track = 0;

            if (! getInt (params, "bus", bus) || ! getInt (params, "track", track))
                return badParameters ("bus and track");

            double number = 0.0;
            bool flag = false;

            if (method == M::setTrackVolume)
                return getDouble (params, "volume", number) ? Outcome::from (core.setTrackVolume (bus, track, number)) : badParameters ("volume");

            if (method == M::setTrackBalance)
                return getDouble (params, "balance", number) ? Outcome::from (core.setTrackBalance (bus, track, number)) : badParameters ("balance");

            if (method == M::setTrackMute)
                return getBool (params, "mute", flag) ? Outcome::from (core.setTrackMute (bus, track, flag)) : badParameters ("mute");

            return getBool (params, "solo", flag) ? Outcome::from (core.setTrackSolo (bus, track, flag)) : badParameters ("solo");
        }

        if (method == M::setBusVolume || method == M::setBusBalance || method == M::setMetronomeVolume)
        {
            int bus = 0;
            double number = 0.0;

            if (! getInt (params, "bus", bus))
                return badParameters ("bus");

            if (method == M::setBusBalance)
                return getDouble (params, "balance", number) ? Outcome::from (core.setBusBalance (bus, number)) : badParameters ("balance");

            if (! getDouble (params, "volume", number))
                return badParameters ("volume");

            return Outcome::from (method == M::setBusVolume ? core.setBusVolume (bus, number)
                                                                   : core.setMetronomeVolume (bus, number));
        }

        if (method == M::setMetronomeState)
        {
            std::string state;

            if (! getString (params, "state", state))
                return badParameters ("state");

            return Outcome::from (core.setMetronomeState (state));
        }

        if (method == M::getEffects || method == M::getEffectParameters || method == M::setEffectParameter || method == M::setEffectBypass)
        {
            MBoxEffectTarget target;

            if (! getTarget (params, target))
                return badParameters ("target");

            if (method == M::getEffects)
            {
                juce::Array<juce::var> effects;

                for (const auto& effect : core.getEffects (target))
                {
                    auto entry = makeObject();
                    set (entry, "name", juce::String (effect.m_name));
                    set (entry, "manufacturer", juce::String (effect.m_manufacturer));
                    set (entry, "bypassed", effect.m_bypassed);
                    effects.add (entry);
                }

                auto result = makeObject();
                set (result, "effects", effects);
                return Outcome::ok (result);
            }

            int effect = 0;

            if (! getInt (params, "effect", effect))
                return badParameters ("effect");

            if (method == M::getEffectParameters)
            {
                juce::Array<juce::var> parameters;

                for (const auto& parameter : core.getEffectParameters (target, effect))
                {
                    auto entry = makeObject();
                    set (entry, "id", juce::String (parameter.m_id));
                    set (entry, "name", juce::String (parameter.m_name));
                    set (entry, "value", parameter.m_value);
                    parameters.add (entry);
                }

                auto result = makeObject();
                set (result, "parameters", parameters);
                return Outcome::ok (result);
            }

            if (method == M::setEffectBypass)
            {
                bool bypassed = false;
                return getBool (params, "bypassed", bypassed) ? Outcome::from (core.setEffectBypass (target, effect, bypassed))
                                                              : badParameters ("bypassed");
            }

            std::string parameterId;
            double value = 0.0;

            if (! getString (params, "parameterId", parameterId) || ! getDouble (params, "value", value))
                return badParameters ("parameterId and value");

            return Outcome::from (core.setEffectParameter (target, effect, parameterId, (float) value));
        }

        return Outcome::error (MBoxComAPI::unknownMethod, "Unknown method " + method);
    }
}

//==============================================================================
// Requests
//==============================================================================
juce::String MBoxServer::handleRequestLine (const juce::String& line, bool& helloDone)
{
    juce::var request;
    auto response = makeObject();

    auto sendError = [&] (int code, const juce::String& message)
    {
        auto error = makeObject();
        set (error, "code", code);
        set (error, "message", message);
        set (response, "error", error);
        return toCompactJson (response);
    };

    if (line.length() > MBoxComAPI::maxMessageBytes || juce::JSON::parse (line, request).failed() || ! request.isObject())
    {
        set (response, "id", {});
        return sendError (MBoxComAPI::parseError, "Expected one JSON object per line");
    }

    set (response, "id", request["id"]);
    const auto method = request["method"].toString();
    const auto params = request["params"];

    if (method.isEmpty())
        return sendError (MBoxComAPI::parseError, "Missing method");

    if (method == MBoxComAPI::Method::hello)
    {
        int version = 0;

        if (! getInt (params, "protocolVersion", version))
            return sendError (MBoxComAPI::badParameters, "hello needs protocolVersion");

        if (version != MBoxComAPI::protocolVersion)
            return sendError (MBoxComAPI::unsupportedVersion,
                              "Protocol version " + juce::String (version) + " is not supported (expected "
                                  + juce::String (MBoxComAPI::protocolVersion) + ")");

        helloDone = true;
        auto result = makeObject();
        set (result, "protocolVersion", MBoxComAPI::protocolVersion);
        set (result, "server", "MBox " MBOX_VERSION_STRING);
        set (response, "result", result);
        return toCompactJson (response);
    }

    if (! helloDone && method != MBoxComAPI::Method::ping)
        return sendError (MBoxComAPI::helloRequired, "Send hello first");

    const auto outcome = dispatch (m_core, method, params);

    if (outcome.m_code != 0)
        return sendError (outcome.m_code, outcome.m_message);

    set (response, "result", outcome.m_result);
    return toCompactJson (response);
}

