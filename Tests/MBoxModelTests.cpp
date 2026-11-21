#include "MBoxTestUtilities.h"
#include "Model/MBoxPlaylist.h"

//==============================================================================
// The model's rules: names, track limits and ids, mixes, signature clamping, locators
//==============================================================================
class MBoxModelTests final : public juce::UnitTest
{
public:
    MBoxModelTests() : juce::UnitTest ("MBoxModel", "MBox") {}

    void runTest() override
    {
        beginTest ("Names must be valid folder names");
        {
            expect (validateItemName ("Set list 1").wasOk());
            expect (validateItemName ("").failed());
            expect (validateItemName (" padded").failed());
            expect (validateItemName ("ends with dot.").failed());
            expect (validateItemName ("a/b").failed());
            expect (validateItemName ("what?").failed());
            expect (validateItemName ("CON").failed());
            expect (validateItemName ("con.txt").failed());
            expect (validateItemName (juce::String::repeatedString ("x", 65)).failed());
        }

        beginTest ("Playlist: unique song names ignoring case, moves, renames");
        {
            MBoxPlaylist playlist ("Live");
            expect (playlist.addSong ("Intro").wasOk());
            expect (playlist.addSong ("Outro").wasOk());
            expect (playlist.addSong ("intro").failed());
            expect (playlist.moveSong ("Outro", 0).wasOk());
            expectEquals (playlist.getSongNames().joinIntoString (","), juce::String ("Outro,Intro"));
            expect (playlist.moveSong ("Outro", 2).failed());
            expect (playlist.renameSong ("Intro", "Outro").failed());
            expect (playlist.renameSong ("Intro", "INTRO").wasOk());  // case only
            expect (playlist.getSongInstance ("intro") != nullptr);
            expect (playlist.removeSong ("Nope").failed());
        }

        beginTest ("Tracks: ids, the 16-track limit and one mix per bus");
        {
            MBoxSong song ("Song");
            int id = 0;

            for (int i = 0; i < maxNumberOfTrackPerSong; ++i)
                expect (song.addTrack (std::make_unique<MBoxInfoTrack> (0, "Info"), &id).wasOk());

            expectEquals (id, maxNumberOfTrackPerSong);
            expect (song.addTrack (std::make_unique<MBoxInfoTrack> (0, "One too many")).failed());

            for (int bus = 1; bus <= numberOfMixBus; ++bus)
                expectEquals ((int) song.getMix (bus).size(), maxNumberOfTrackPerSong);

            expect (song.removeTrack (3).wasOk());
            expect (song.getTrackMix (1, 3) == nullptr);
            expectEquals ((int) song.getMix (2).size(), maxNumberOfTrackPerSong - 1);

            // Ids are never reused while a higher one exists
            expect (song.addTrack (std::make_unique<MBoxInfoTrack> (0, "New"), &id).wasOk());
            expectEquals (id, maxNumberOfTrackPerSong + 1);
        }

        beginTest ("Grids: bar 1 entries are required, signatures only at bar starts");
        {
            MBoxSong song ("Song");
            expect (song.removeTempo (Bar {}).failed());
            expect (song.removeSignature (Bar {}).failed());
            expect (song.addTempo ({ { 5, 1, 1, 0 }, 10 }).failed());
            expect (song.addTempo ({ { 5, 2, 3, 100 }, 140 }).wasOk());
            expect (song.addSignature ({ { 5, 2, 1, 0 }, { 3, 4 } }).failed());
            expect (song.addSignature ({ { 5, 1, 1, 0 }, { 5, 3 } }).failed());
            expect (song.addSignature ({ { 9, 1, 1, 0 }, { 6, 8 } }).wasOk());
            expectEquals ((int) song.getSignatureGrid().size(), 2);
        }

        beginTest ("A signature change clamps positions that no longer exist");
        {
            MBoxSong song ("Song");
            int infoId = 0;
            song.addTrack (std::make_unique<MBoxInfoTrack> (0, "Cues"), &infoId);
            dynamic_cast<MBoxInfoTrack*> (song.getTrack (infoId))->addInfoClick ({ 3, 4, 1, 0 });
            expect (song.addTempo ({ { 3, 4, 2, 0 }, 90 }).wasOk());

            expect (song.addSignature ({ { 3, 1, 1, 0 }, { 3, 4 } }).wasOk());

            const auto& clicks = dynamic_cast<MBoxInfoTrack*> (song.getTrack (infoId))->getInfoClicks();
            expect (clicks.front() == Bar { 3, 3, 1, 0 });
            expect (song.getTempoGrid().back().first == Bar { 3, 3, 2, 0 });
        }

        beginTest ("Locators");
        {
            MBoxSong song ("Song");
            expect (song.getRightLocator() == Bar { defaultTotalBar, 1, 1, 0 });
            expect (song.setLeftLocator ({ 40, 1, 1, 0 }).failed());
            expect (song.setLeftLocator ({ 2, 5, 1, 0 }).failed());
            expect (song.setLeftLocator ({ 2, 1, 1, 0 }).wasOk());
            expect (song.setRightLocator ({ 2, 1, 1, 0 }).failed());
            expect (song.setRightLocator ({ 50, 1, 1, 0 }).wasOk());
            expectEquals (song.getTotalBar(), 50);
            song.setEndSongDelay (99);
            expectEquals (song.getEndSongDelay(), maxEndSongDelaySeconds);
        }

        beginTest ("The right locator follows the end of the WAVs, never moving earlier");
        {
            MBoxTest::TemporaryDirectory temp;
            const auto wav = temp.m_directory.getChildFile ("four-seconds.wav");
            expect (MBoxTest::writeWav (wav, 4 * 44100, [] (auto) { return std::pair { 0.1f, 0.1f }; }).wasOk());

            MBoxSong song ("Song");
            int id = 0;
            song.addTrack (std::make_unique<MBoxAudioTrack> (0, "Backing"), &id);
            expect (song.setAudioStart (id, { 31, 1, 1, 0 }).wasOk());
            expect (song.setAudioFile (id, wav).wasOk());

            // 4 s at 120 BPM in 4/4 is 2 bars
            expect (song.getRightLocator() == Bar { 33, 1, 1, 0 }, song.getRightLocator().toString());
            expectEquals (song.getTotalBar(), 33);

            // Twice as fast: the WAV now spans 4 bars
            expect (song.addTempo ({ Bar {}, 240 }).wasOk());
            expect (song.getRightLocator() == Bar { 35, 1, 1, 0 }, song.getRightLocator().toString());

            // Slower again: the locator stays
            expect (song.addTempo ({ Bar {}, 120 }).wasOk());
            expect (song.getRightLocator() == Bar { 35, 1, 1, 0 });
        }

        beginTest ("Only 44100 Hz, 16-bit stereo WAVs are accepted");
        {
            MBoxTest::TemporaryDirectory temp;
            auto silence = [] (auto) { return std::pair { 0.0f, 0.0f }; };
            const auto at48k = temp.m_directory.getChildFile ("48k.wav");
            const auto in24bits = temp.m_directory.getChildFile ("24.wav");
            const auto inMono = temp.m_directory.getChildFile ("mono.wav");
            MBoxTest::writeWav (at48k, 100, silence, 48000.0);
            MBoxTest::writeWav (in24bits, 100, silence, 44100.0, 24);
            MBoxTest::writeWav (inMono, 100, silence, 44100.0, 16, 1);

            std::int64_t length = 0;
            expect (readSupportedWavLength (at48k, length).failed());
            expect (readSupportedWavLength (in24bits, length).failed());
            expect (readSupportedWavLength (inMono, length).failed());
            expect (readSupportedWavLength (temp.m_directory.getChildFile ("missing.wav"), length).failed());
        }

        beginTest ("Slot automation: the first slot applies before the first change");
        {
            MBoxPhysicalTrack track (1, "Guitar");
            expect (! track.getSlotAt (Bar {}).has_value());
            track.setBankSlotChange ({ 9, 1, 1, 0 }, { 2, 3 });
            track.setBankSlotChange ({ 5, 1, 1, 0 }, { 1, 1 });
            expect (*track.getSlotAt ({ 1, 1, 1, 0 }) == MBoxSlotRef { 1, 1 });
            expect (*track.getSlotAt ({ 8, 4, 4, 239 }) == MBoxSlotRef { 1, 1 });
            expect (*track.getSlotAt ({ 9, 1, 1, 0 }) == MBoxSlotRef { 2, 3 });
            track.setBankSlotChange ({ 9, 1, 1, 0 }, { 4, 4 });
            expectEquals ((int) track.getBankSlotChanges().size(), 2);
        }

        beginTest ("Copies are deep");
        {
            MBoxPlaylist playlist ("Live");
            playlist.addSong ("A");
            playlist.getSongInstance ("A")->addTrack (std::make_unique<MBoxPhysicalTrack> (0, "Guitar"));

            const MBoxPlaylist copy (playlist);
            playlist.getSongInstance ("A")->getTrack (1)->setName ("Changed");
            expectEquals (copy.getSongInstance ("A")->getTrack (1)->getName(), juce::String ("Guitar"));
        }
    }
};

static MBoxModelTests modelTests;
