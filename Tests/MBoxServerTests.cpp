#include "MBoxTestUtilities.h"
#include "TCPServer/MBoxServer.h"

//==============================================================================
// The public API end to end (with the null audio device) and the TCP protocol, both through
// handleRequestLine and through a real socket
//==============================================================================
class MBoxServerTests final : public juce::UnitTest
{
public:
    MBoxServerTests() : juce::UnitTest ("MBoxServer", "MBox") {}

    void runTest() override
    {
        MBoxTest::TemporaryDirectory temp;
        MBoxCoreOptions options;
        options.m_dataDirectory = temp.m_directory.getFullPathName().toStdString();
        options.m_tcpPort = 0;
        options.m_openAudioDevice = false;

        {
            MBoxCoreAudio core (options);

            beginTest ("Only one instance per process");
            {
                expect (core.getInitialisationResult().wasOk(), core.getInitialisationResult().m_message);
                MBoxCoreAudio second (options);
                expect (! second.getInitialisationResult().wasOk());
            }

            testProtocol (core);
            testPlayback (core, temp.m_directory);
            testSocket (core);
        }

        beginTest ("Data is saved on destruction and loaded by the next instance");
        {
            MBoxCoreAudio core (options);
            expect (core.getInitialisationResult().wasOk(), core.getInitialisationResult().m_message);
            expect (core.getPlaylistNames() == std::vector<std::string> { "Set" });
            expect (core.getSongNames ("Set") == std::vector<std::string> { "First", "Second" });
        }
    }

private:
    void testProtocol (MBoxCoreAudio& core)
    {
        beginTest ("Protocol: hello first, versions, errors");

        MBoxServer server (core);
        bool hello = false;

        auto request = [&] (const juce::String& line)
        {
            return juce::JSON::parse (server.handleRequestLine (line, hello));
        };

        expectEquals ((int) request ("{\"id\":1,\"method\":\"getPlaylists\"}")["error"]["code"], (int) MBoxComAPI::helloRequired);
        expect (request ("{\"id\":2,\"method\":\"ping\"}").hasProperty ("result"));
        expectEquals ((int) request ("not json")["error"]["code"], (int) MBoxComAPI::parseError);
        expectEquals ((int) request ("{\"id\":3,\"method\":\"hello\",\"params\":{\"protocolVersion\":2}}")["error"]["code"],
                      (int) MBoxComAPI::unsupportedVersion);

        const auto helloResponse = request ("{\"id\":4,\"method\":\"hello\",\"params\":{\"protocolVersion\":1,\"client\":\"test\"}}");
        expectEquals ((int) helloResponse["id"], 4);
        expectEquals ((int) helloResponse["result"]["protocolVersion"], 1);
        expect (hello);

        expectEquals ((int) request ("{\"id\":5,\"method\":\"nope\"}")["error"]["code"], (int) MBoxComAPI::unknownMethod);
        expectEquals ((int) request ("{\"id\":6,\"method\":\"getSongs\",\"params\":{}}")["error"]["code"], (int) MBoxComAPI::badParameters);
        expectEquals ((int) request ("{\"id\":7,\"method\":\"setTrackVolume\",\"params\":{\"bus\":1,\"track\":1,\"volume\":\"loud\"}}")["error"]["code"],
                      (int) MBoxComAPI::badParameters);
        expectEquals ((int) request ("{\"id\":8,\"method\":\"selectSong\",\"params\":{\"playlist\":\"x\",\"song\":\"y\"}}")["error"]["code"],
                      (int) MBoxComAPI::rejected);
        expect (request ("{\"id\":9,\"method\":\"getPlaylists\"}")["result"]["playlists"].isArray());

        beginTest ("Protocol: getPlaylistNames, renamePlaylist, renameSong");

        expect (core.addPlaylist ("RenameMe").wasOk());
        expect (core.addSong ("RenameMe", "OldSong").wasOk());
        expect (core.addPlaylist ("Taken").wasOk());

        auto contains = [] (const juce::var& names, const juce::String& name)
        {
            if (auto* array = names.getArray())
                return array->contains (name);

            return false;
        };

        expect (contains (request ("{\"id\":10,\"method\":\"getPlaylistNames\"}")["result"]["playlists"], "RenameMe"));

        expectEquals ((int) request ("{\"id\":11,\"method\":\"renamePlaylist\",\"params\":{\"playlist\":\"RenameMe\"}}")["error"]["code"],
                      (int) MBoxComAPI::badParameters);
        expectEquals ((int) request ("{\"id\":12,\"method\":\"renamePlaylist\",\"params\":{\"playlist\":\"RenameMe\",\"newName\":\"Taken\"}}")["error"]["code"],
                      (int) MBoxComAPI::rejected);
        expect (request ("{\"id\":13,\"method\":\"renamePlaylist\",\"params\":{\"playlist\":\"RenameMe\",\"newName\":\"Renamed\"}}").hasProperty ("result"));

        const auto names = request ("{\"id\":14,\"method\":\"getPlaylistNames\"}")["result"]["playlists"];
        expect (contains (names, "Renamed"));
        expect (! contains (names, "RenameMe"));

        expectEquals ((int) request ("{\"id\":15,\"method\":\"renameSong\",\"params\":{\"playlist\":\"Renamed\",\"song\":\"OldSong\"}}")["error"]["code"],
                      (int) MBoxComAPI::badParameters);
        expect (request ("{\"id\":16,\"method\":\"renameSong\",\"params\":{\"playlist\":\"Renamed\",\"song\":\"OldSong\",\"newName\":\"NewSong\"}}").hasProperty ("result"));
        expect (contains (request ("{\"id\":17,\"method\":\"getSongs\",\"params\":{\"playlist\":\"Renamed\"}}")["result"]["songs"], "NewSong"));
        expectEquals ((int) request ("{\"id\":18,\"method\":\"renameSong\",\"params\":{\"playlist\":\"Renamed\",\"song\":\"Missing\",\"newName\":\"X\"}}")["error"]["code"],
                      (int) MBoxComAPI::rejected);

        // Leave the data as the following tests expect it
        expect (core.removePlaylist ("Renamed").wasOk());
        expect (core.removePlaylist ("Taken").wasOk());
    }

    void testPlayback (MBoxCoreAudio& core, const juce::File& dataDirectory)
    {
        beginTest ("API: build a playlist, load it, play, mix");

        std::vector<MBoxEvent> events;
        const auto listener = core.addEventListener ([&] (const MBoxEvent& event) { events.push_back (event); });
        auto hasEvent = [&] (const std::string& name, const std::string& fragment = {})
        {
            return std::any_of (events.begin(), events.end(), [&] (const auto& event)
                                { return event.m_name == name && event.m_json.find (fragment) != std::string::npos; });
        };

        expect (core.addPlaylist ("Set").wasOk());
        expect (! core.addPlaylist ("set").wasOk());
        expect (core.addSong ("Set", "First").wasOk());
        expect (core.addSong ("Set", "Second").wasOk());

        const auto wav = dataDirectory.getChildFile ("playlists/Set/songs/First/tracks/media/backing.wav");
        expect (MBoxTest::writeWav (wav, 44100, [] (auto) { return std::pair { 0.1f, 0.1f }; }).wasOk());

        auto* song = core.getSongInstance ("Set", "First");
        int trackId = 0;
        expect (song->addTrack (std::make_unique<MBoxAudioTrack> (0, "Backing"), &trackId).wasOk());
        expect (song->setAudioFile (trackId, wav).wasOk());
        expect (core.songChanged ("Set", "First").wasOk());

        expect (! core.play().wasOk());  // nothing loaded yet
        expect (core.selectSong ("Set", "First").wasOk());
        expect (MBoxTest::waitUntil ([&] { return hasEvent (MBoxComAPI::Event::playlistLoaded, "\"ok\":true"); }));
        expectEquals (juce::String (core.getTransportInfo().m_song), juce::String ("First"));

        expect (core.play().wasOk());
        expect (MBoxTest::waitUntil ([&] { return core.getTransportInfo().m_state == "playing"
                                                  && core.getTransportInfo().m_position != "1.1.1.0"; }));
        expect (MBoxTest::waitUntil ([&] { return hasEvent (MBoxComAPI::Event::transport, "\"state\":\"playing\""); }));

        expect (core.setTrackVolume (2, trackId, -12.0).wasOk());
        expect (hasEvent (MBoxComAPI::Event::mixChanged, "\"volume\":-12"));
        expect (core.getMixJson (2).find ("\"volume\":-12") != std::string::npos);
        expect (! core.setTrackVolume (4, trackId, 0.0).wasOk());
        expect (! core.setMetronomeState ("loud").wasOk());

        expect (core.stop().wasOk());
        expect (MBoxTest::waitUntil ([&] { return core.getTransportInfo().m_state == "stopped"; }));

        // Within the loaded playlist, selecting is immediate
        expect (core.selectSong ("Set", "Second").wasOk());
        expect (MBoxTest::waitUntil ([&] { return core.getTransportInfo().m_song == "Second"; }));

        // Renaming moves the folder and the WAV path with it
        expect (core.renameSong ("Set", "First", "Opener").wasOk());
        expect (core.getSongInstance ("Set", "Opener") != nullptr);
        const auto* audio = dynamic_cast<MBoxAudioTrack*> (core.getSongInstance ("Set", "Opener")->getTrack (trackId));
        expect (audio->getFilename().existsAsFile());
        expect (core.renameSong ("Set", "Opener", "First").wasOk());

        expect (core.save().wasOk());
        expect (dataDirectory.getChildFile ("playlists/Set/songs/First/song.json").existsAsFile());
        core.removeEventListener (listener);
    }

    void testSocket (MBoxCoreAudio& core)
    {
        beginTest ("Protocol over a real TCP socket");

        MBoxServer server (core);
        expect (server.start (0).wasOk());

        juce::StreamingSocket client;
        expect (client.connect ("127.0.0.1", server.getPort(), 2000));

        juce::String received;

        auto readLine = [&]
        {
            const auto gotLine = MBoxTest::waitUntil ([&]
            {
                char buffer[1024];

                while (client.waitUntilReady (true, 0) == 1)
                {
                    const auto numRead = client.read (buffer, (int) sizeof (buffer), false);

                    if (numRead <= 0)
                        return true;

                    received += juce::String::fromUTF8 (buffer, numRead);
                }

                return received.containsChar ('\n');
            }, 5000);

            const auto line = received.upToFirstOccurrenceOf ("\n", false, false);
            received = received.fromFirstOccurrenceOf ("\n", false, false);
            return gotLine ? juce::JSON::parse (line) : juce::var();
        };

        const juce::String hello ("{\"id\":1,\"method\":\"hello\",\"params\":{\"protocolVersion\":1}}\n");
        client.write (hello.toRawUTF8(), (int) hello.getNumBytesAsUTF8());
        expectEquals ((int) readLine()["result"]["protocolVersion"], 1);

        server.broadcast ({ "test", "{\"value\":3}" });
        const auto event = readLine();
        expectEquals (event["event"].toString(), juce::String ("test"));
        expectEquals ((int) event["data"]["value"], 3);

        expect (MBoxTest::waitUntil ([&] { return server.getNumClients() == 1; }));
        client.close();
    }
};

static MBoxServerTests serverTests;
