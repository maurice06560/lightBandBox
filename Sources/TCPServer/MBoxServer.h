#pragma once

#include "MBoxComAPI.h"
#include "../MBoxCoreAudio.h"

#include <juce_events/juce_events.h>

//==============================================================================
// The TCP server for the ESP32 controller and other clients (see MBoxComAPI.h for the
// protocol).
//
// Threads: one thread accepts connections, and one thread per client reads its lines. Every
// request is passed to the message thread, where it's handled with the MBoxCoreAudio public
// API, and its response written back from there. Events are broadcast from the message
// thread too. Socket writes are serialised per client by a lock, so a slow client never
// blocks the audio thread (which the server never touches).
//==============================================================================
class MBoxServer
{
public:
    explicit MBoxServer (MBoxCoreAudio& core);
    ~MBoxServer();

    // Listens on every network interface. Port 0 picks a free port (see getPort()).
    juce::Result start (int port);
    void stop();
    int getPort() const noexcept  { return m_port; }
    int getNumClients() const;

    // Message thread. Sends an event to every client that has said hello.
    void broadcast (const MBoxEvent& event);

    // Message thread. Handles one request line and returns the response line (without the
    // '\n'). helloDone is the client's session state and is updated by "hello".
    // Public so the protocol can be tested without sockets.
    juce::String handleRequestLine (const juce::String& line, bool& helloDone);

private:
    class Client;
    class Acceptor;

    void removeDeadClients();

    MBoxCoreAudio& m_core;
    int m_port { 0 };
    std::unique_ptr<Acceptor> m_acceptor;
    mutable juce::CriticalSection m_clientsLock;
    std::vector<std::shared_ptr<Client>> m_clients;
    int m_nextClientId { 1 };

    JUCE_DECLARE_WEAK_REFERENCEABLE (MBoxServer)
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MBoxServer)
};
