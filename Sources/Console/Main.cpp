#include <juce_core/juce_core.h>

#include "../Communication/MBoxComAPI.h"

#include <cstring>
#include <iostream>
#include <optional>
#include <string>

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

    juce::StringArray splitLines (const juce::String& text)
    {
        juce::StringArray lines;
        lines.addTokens (text, "\n", "");
        lines.removeEmptyStrings();
        return lines;
    }
}

//==============================================================================
/** Synchronous client: sends a Request and waits for the matching Response.
    Events received while waiting are printed. */
class MBoxClient final
{
public:
    bool connect (const juce::String& host, int port) { return m_socket.connect (host, port, 3000); }

    /** Returns the results of the call, or nullopt if the connection was lost. */
    std::optional<juce::String> call (const juce::String& methodName, const juce::StringArray& parameters = {})
    {
        Message request {};
        request.requestId = ++m_lastRequestId;
        request.type = MessageType::Request;
        writeField (request.methodName, fieldLength, methodName);

        for (int i = 0; i < juce::jmin (parameters.size(), (int) maxParameters); ++i)
            writeField (request.parameters[i], fieldLength, parameters[i]);

        if (m_socket.write (&request, (int) sizeof (Message)) != (int) sizeof (Message))
            return std::nullopt;

        for (;;)
        {
            Message reply {};

            if (m_socket.read (&reply, (int) sizeof (Message), true) != (int) sizeof (Message))
                return std::nullopt;

            if (reply.type == MessageType::Event)
                std::cout << "[event] " << readField (reply.methodName, fieldLength)
                          << ": " << readField (reply.results, resultsLength) << "\n";
            else if (reply.type == MessageType::Response && reply.requestId == request.requestId)
                return readField (reply.results, resultsLength);
        }
    }

private:
    juce::StreamingSocket m_socket;
    std::uint32_t m_lastRequestId = 0;
};

//==============================================================================
class Console final
{
public:
    explicit Console (MBoxClient& client) : m_client (client) {}

    int run()
    {
        for (;;)
        {
            std::cout << "\n=== OneBox Console ===\n"
                      << "  1. List playlists\n"
                      << "  2. List songs\n"
                      << "  3. Select song\n"
                      << "  4. Play\n"
                      << "  5. Pause\n"
                      << "  6. Stop\n"
                      << "  7. Quit OneBox application\n"
                      << "  0. Exit console\n";

            const auto choice = prompt ("> ");

            if (! choice.has_value() || *choice == "0")
                return 0;

            bool ok = true;

            if      (*choice == "1") ok = listPlaylists().has_value();
            else if (*choice == "2") ok = listSongs();
            else if (*choice == "3") ok = selectSong();
            else if (*choice == "4") ok = simpleCall ("play");
            else if (*choice == "5") ok = simpleCall ("pause");
            else if (*choice == "6") ok = simpleCall ("stop");
            else if (*choice == "7") { simpleCall ("quit"); return 0; }
            else std::cout << "Unknown choice\n";

            if (! ok)
            {
                std::cout << "Connection lost\n";
                return 1;
            }
        }
    }

private:
    static std::optional<juce::String> prompt (const juce::String& text)
    {
        std::cout << text << std::flush;

        std::string line;
        if (! std::getline (std::cin, line))
            return std::nullopt;

        return juce::String (line).trim();
    }

    /** Prints the items numbered from 1 and lets the user pick one. Returns an empty string if cancelled. */
    static juce::String choose (const juce::String& title, const juce::StringArray& items)
    {
        if (items.isEmpty())
        {
            std::cout << "No " << title.toLowerCase() << "\n";
            return {};
        }

        std::cout << title << ":\n";
        for (int i = 0; i < items.size(); ++i)
            std::cout << "  " << (i + 1) << ". " << items[i] << "\n";

        const auto choice = prompt ("Number (empty to cancel): ").value_or ({});
        const auto index = choice.getIntValue() - 1;

        if (choice.isEmpty() || ! juce::isPositiveAndBelow (index, items.size()))
            return {};

        return items[index];
    }

    bool simpleCall (const juce::String& methodName)
    {
        const auto result = m_client.call (methodName);

        if (result.has_value())
            std::cout << methodName << ": " << *result << "\n";

        return result.has_value();
    }

    std::optional<juce::StringArray> fetchList (const juce::String& methodName, const juce::StringArray& parameters = {})
    {
        if (const auto result = m_client.call (methodName, parameters))
            return splitLines (*result);

        return std::nullopt;
    }

    std::optional<juce::StringArray> listPlaylists()
    {
        const auto playlists = fetchList ("getPlaylistNames");

        if (playlists.has_value())
        {
            std::cout << "Playlists:\n";
            for (const auto& name : *playlists)
                std::cout << "  - " << name << "\n";
        }

        return playlists;
    }

    /** Asks the user to pick a playlist. Returns nullopt if the connection was lost, an empty string if cancelled. */
    std::optional<juce::String> choosePlaylist()
    {
        const auto playlists = fetchList ("getPlaylistNames");

        if (! playlists.has_value())
            return std::nullopt;

        return choose ("Playlists", *playlists);
    }

    bool listSongs()
    {
        const auto playlist = choosePlaylist();

        if (! playlist.has_value())
            return false;

        if (playlist->isEmpty())
            return true;

        const auto songs = fetchList ("getSongNames", { *playlist });

        if (songs.has_value())
        {
            std::cout << "Songs in '" << *playlist << "':\n";
            for (const auto& name : *songs)
                std::cout << "  - " << name << "\n";
        }

        return songs.has_value();
    }

    bool selectSong()
    {
        const auto playlist = choosePlaylist();

        if (! playlist.has_value())
            return false;

        if (playlist->isEmpty())
            return true;

        const auto songs = fetchList ("getSongNames", { *playlist });

        if (! songs.has_value())
            return false;

        const auto song = choose ("Songs", *songs);

        if (song.isEmpty())
            return true;

        const auto result = m_client.call ("selectSong", { *playlist, song });

        if (result.has_value())
            std::cout << "selectSong '" << *playlist << "' / '" << song << "': " << *result << "\n";

        return result.has_value();
    }

    MBoxClient& m_client;
};

//==============================================================================
int main (int argc, char* argv[])
{
    const juce::String host = argc > 1 ? juce::String (argv[1]) : juce::String ("127.0.0.1");
    const int port = argc > 2 ? juce::String (argv[2]).getIntValue() : 5050;

    MBoxClient client;

    if (! client.connect (host, port))
    {
        std::cerr << "Unable to connect to " << host << ":" << port << "\n";
        return 1;
    }

    std::cout << "Connected to " << host << ":" << port << "\n";

    Console console (client);
    return console.run();
}
