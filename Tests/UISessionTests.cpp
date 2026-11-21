#include "MBoxTestUtilities.h"

#include "../Applications/UI/Session.h"

//==============================================================================
// The UI's Session against a real MBoxCoreAudio (null audio device): selection, edits through
// the ...Changed() methods, live mixing, the edit lock, WAV import and following the
// library's events
//==============================================================================
class UISessionTests final : public juce::UnitTest
{
public:
    UISessionTests() : juce::UnitTest ("UISession", "MBox") {}

    void runTest() override
    {
        MBoxTest::TemporaryDirectory temp;
        MBoxCoreOptions options;
        options.m_dataDirectory = temp.m_directory.getFullPathName().toStdString();
        options.m_tcpPort = 0;
        options.m_openAudioDevice = false;

        MBoxCoreAudio core (options);
        Session session (core);
        Counter counter;
        session.addListener (&counter);

        beginTest ("An empty data folder selects nothing");
        {
            expect (core.getInitialisationResult().wasOk(), core.getInitialisationResult().m_message);
            session.selectFirstSong();
            expect (session.getSelectedPlaylistName().isEmpty());
            expect (session.getSelectedSong() == nullptr);
        }

        beginTest ("Creating a playlist and a song selects and loads them");
        {
            expect (session.createPlaylist ("Set").wasOk());
            expectEquals (session.getSelectedPlaylistName(), juce::String ("Set"));
            expect (session.createSong ("Set", "First").wasOk());
            expect (session.createSong ("Set", "Second").wasOk());
            expectEquals (session.getSelectedSongName(), juce::String ("Second"));
            expect (MBoxTest::waitUntil ([&] { return session.isSelectionLoaded(); }));
            expect (! session.createPlaylist ("set").wasOk());
            expect (session.isStatusError());
        }

        beginTest ("Song edits reach the model and select the new track");
        {
            const auto before = counter.m_changed;
            int trackId = 0;
            expect (session.editSong ([&] (MBoxSong& song) { return song.addTrack (std::make_unique<MBoxAudioTrack> (0, "Backing"), &trackId); },
                                      "Track added").wasOk());
            expect (counter.m_changed > before);
            expectEquals (session.getStatus(), juce::String ("Track added"));
            expectEquals (session.getSelectedTrackId(), trackId);
            expect (session.getTrackMix (trackId) != nullptr);
        }

        beginTest ("Live mixing waits for the load, then reaches the library");
        {
            const auto trackId = session.getSelectedTrackId();
            expect (MBoxTest::waitUntil ([&] { return session.isSelectionLoaded(); }));

            const auto mixBefore = counter.m_mix;
            expect (session.setTrackVolume (trackId, -6.0).wasOk());
            expectEquals (session.getTrackMix (trackId)->m_volume, -6.0);
            expect (counter.m_mix > mixBefore);

            session.selectBus (2);
            expect (session.setMasterVolume (-3.0).wasOk());
            expectEquals (session.getRig().m_mixBuses[1].getMasterVolume(), -3.0);
            expectEquals (session.getRig().m_mixBuses[0].getMasterVolume(), 0.0);
            session.selectBus (1);

            session.selectSong ("Set", "First");
            expect (! session.isSelectionLoaded() || session.getSelectedSongName() == "First");
            expect (MBoxTest::waitUntil ([&] { return session.isSelectionLoaded(); }));
            expectEquals (juce::String (core.getTransportInfo().m_song), juce::String ("First"));
        }

        beginTest ("Edits are refused while playing; the song end and live mixing aren't");
        {
            expect (session.play().wasOk());
            expect (MBoxTest::waitUntil ([&] { return session.isPlaying(); }));
            expect (session.isEditLocked());
            expect (! session.editSong ([] (MBoxSong&) { return juce::Result::ok(); }).wasOk());
            expect (! session.renameSong ("Set", "First", "Renamed").wasOk());
            expect (session.setSongEnd ("Set", "First", EndSongAction::Continue, 3).wasOk());
            expect (core.getSongInstance ("Set", "First")->getEndSongAction() == EndSongAction::Continue);
            expect (session.setMetronomeVolume (-12.0).wasOk());

            session.stop();
            expect (MBoxTest::waitUntil ([&] { return ! session.isEditLocked(); }));
        }

        beginTest ("Renames are followed by the selection");
        {
            expect (session.renameSong ("Set", "First", "Opener").wasOk());
            expectEquals (session.getSelectedSongName(), juce::String ("Opener"));
            expect (session.renamePlaylist ("Set", "Tour").wasOk());
            expectEquals (session.getSelectedPlaylistName(), juce::String ("Tour"));
            expect (session.getSelectedSong() != nullptr);
        }

        beginTest ("Importing a WAV copies it into the song's media folder");
        {
            const auto source = temp.m_directory.getChildFile ("outside/click.wav");
            expect (MBoxTest::writeWav (source, 44100 * 2, [] (auto) { return std::pair { 0.1f, 0.1f }; }).wasOk());

            int trackId = 0;
            expect (session.editSong ([&] (MBoxSong& song) { return song.addTrack (std::make_unique<MBoxAudioTrack> (0, "Click"), &trackId); }).wasOk());
            expect (session.importWav (trackId, Bar { 2, 1, 1, 0 }, source).wasOk(), session.getStatus());

            auto* track = dynamic_cast<MBoxAudioTrack*> (session.getSelectedSong()->getTrack (trackId));
            const auto copied = temp.m_directory.getChildFile ("playlists/Tour/songs/Opener/tracks/media/click.wav");
            expect (copied.existsAsFile());
            expect (track->getFilename() == copied);
            expect (track->getAudioStart() == Bar { 2, 1, 1, 0 });

            // A different file with the same name gets its own copy
            const auto other = temp.m_directory.getChildFile ("elsewhere/click.wav");
            expect (MBoxTest::writeWav (other, 44100, [] (auto) { return std::pair { 0.2f, 0.2f }; }).wasOk());
            expect (session.importWav (trackId, Bar { 1, 1, 1, 0 }, other).wasOk());
            expect (track->getFilename() != copied);
            expect (track->getFilename().getParentDirectory() == copied.getParentDirectory());

            // An unsupported file copies nothing and leaves the track as it was
            const auto wrongRate = temp.m_directory.getChildFile ("outside/fast.wav");
            expect (MBoxTest::writeWav (wrongRate, 48000, [] (auto) { return std::pair { 0.1f, 0.1f }; }, 48000.0).wasOk());
            const auto fileBefore = track->getFilename();
            expect (! session.importWav (trackId, Bar { 3, 1, 1, 0 }, wrongRate).wasOk());
            expect (session.isStatusError());
            expect (! copied.getParentDirectory().getChildFile ("fast.wav").exists());
            expect (track->getFilename() == fileBefore);
            expect (track->getAudioStart() == Bar { 1, 1, 1, 0 });
        }

        beginTest ("A selection made elsewhere (a TCP client) is followed");
        {
            expect (MBoxTest::waitUntil ([&] { return session.isSelectionLoaded(); }));
            expect (core.selectSong ("Tour", "Second").wasOk());
            expectEquals (session.getSelectedSongName(), juce::String ("Second"));
        }

        beginTest ("Play from the edit cursor, the click per mix, playlist copy and paste");
        {
            expect (MBoxTest::waitUntil ([&] { return session.isSelectionLoaded(); }));

            // Stopped: play starts at the edit cursor (no count-in away from the left locator)
            session.setCursor ({ 3, 1, 1, 0 });
            expect (session.play().wasOk());
            expect (MBoxTest::waitUntil ([&] { return session.getPlayheadFrames().has_value(); }));
            auto* song = session.getSelectedSong();
            expectGreaterOrEqual (*session.getPlayheadFrames(), song->getTimeline().barToFrames ({ 3, 1, 1, 0 }));
            session.stop();
            expect (MBoxTest::waitUntil ([&] { return ! session.isEditLocked(); }));

            session.selectBus (3);
            expect (session.setMetronomeInMix (false).wasOk());
            expect (! session.getRig().m_mixBuses[2].isMetronomeOn());
            session.selectBus (1);

            expect (! session.canPastePlaylist());
            session.copyPlaylist ("Tour");
            expect (session.canPastePlaylist());
            const auto replacedBefore = counter.m_replaced;
            expect (session.pastePlaylist().wasOk());
            expectEquals (session.getSelectedPlaylistName(), juce::String ("Tour copy"));
            expect (session.pastePlaylist().wasOk());
            expect (session.getPlaylistNames().contains ("Tour copy 2"));
            expect (session.deletePlaylist ("Tour copy 2").wasOk());
            expect (session.deletePlaylist ("Tour copy").wasOk());

            // An edit rebuilds the loaded playlist: its plugins are replaced (windows must reopen)
            session.selectSong ("Tour", "Opener");
            expect (MBoxTest::waitUntil ([&] { return session.isSelectionLoaded(); }));
            expect (session.editSong ([] (MBoxSong& s) { s.setTotalBar (s.getTotalBar() + 1); return juce::Result::ok(); }).wasOk());
            expect (MBoxTest::waitUntil ([&] { return counter.m_replaced > replacedBefore && session.isSelectionLoaded(); }));
        }

        beginTest ("Deleting the selected playlist clears the selection");
        {
            const auto replacedBefore = counter.m_replaced;
            expect (session.deletePlaylist ("Tour").wasOk());
            expectGreaterThan (counter.m_replaced, replacedBefore);   // unloaded: plugin windows must close
            expect (session.getSelectedPlaylistName().isEmpty());
            expect (session.getSelectedSong() == nullptr);
            expectEquals (session.getSelectedTrackId(), 0);
        }

        session.removeListener (&counter);
    }

private:
    struct Counter final : Session::Listener
    {
        void sessionChanged() override     { ++m_changed; }
        void sessionMixChanged() override  { ++m_mix; }
        void sessionPlayerReplaced() override  { ++m_replaced; }

        int m_changed { 0 };
        int m_mix { 0 };
        int m_replaced { 0 };
    };
};

static UISessionTests uiSessionTests;
