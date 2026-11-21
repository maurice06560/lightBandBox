#include "MBoxTestUtilities.h"
#include "Persistence/MBoxStorage.h"

//==============================================================================
// Saving and loading the data folder
//==============================================================================
class MBoxStorageTests final : public juce::UnitTest
{
public:
    MBoxStorageTests() : juce::UnitTest ("MBoxStorage", "MBox") {}

    void runTest() override
    {
        beginTest ("An empty folder loads as defaults");
        {
            MBoxTest::TemporaryDirectory temp;
            MBoxPlaylists playlists;
            MBoxRig rig;
            MBoxBanks banks;
            expect (MBoxStorage (temp.m_directory).load (playlists, rig, banks).wasOk());
            expect (playlists.empty());
            expectEquals (banks[4].m_bankSlots[4].m_name, juce::String ("Slot 5"));
            expect (rig.m_mixBuses[2].getOutputs() == std::pair { 4, 5 });
        }

        beginTest ("Save then load gives the same data");
        {
            MBoxTest::TemporaryDirectory temp;
            const MBoxStorage storage (temp.m_directory);
            MBoxPlaylists playlists;
            MBoxRig rig;
            auto banks = createDefaultBanks();
            buildExample (storage, playlists, rig, banks);

            expect (storage.save (playlists, rig, banks).wasOk());
            expect (storage.getMediaDirectory ("Tour", "Opener").isDirectory());

            MBoxPlaylists loadedPlaylists;
            MBoxRig loadedRig;
            MBoxBanks loadedBanks;
            const auto result = storage.load (loadedPlaylists, loadedRig, loadedBanks);
            expect (result.wasOk(), result.getErrorMessage());

            expectEquals ((int) loadedPlaylists.size(), 2);
            expectEquals (loadedPlaylists[0]->getSongNames().joinIntoString (","), juce::String ("Opener,Closer"));

            const auto& song = *playlists[0]->getSongInstance ("Opener");
            const auto& loadedSong = *loadedPlaylists[0]->getSongInstance ("Opener");
            const auto tracksDirectory = storage.getTracksDirectory ("Tour", "Opener");
            expectEquals (toJson (MBoxStorage::songToJson (loadedSong)), toJson (MBoxStorage::songToJson (song)));
            expectEquals (toJson (MBoxStorage::tracksToJson (loadedSong, tracksDirectory)),
                          toJson (MBoxStorage::tracksToJson (song, tracksDirectory)));
            expectEquals (toJson (MBoxStorage::rigToJson (loadedRig)), toJson (MBoxStorage::rigToJson (rig)));
            expectEquals (toJson (MBoxStorage::banksToJson (loadedBanks)), toJson (MBoxStorage::banksToJson (banks)));

            // WAVs inside the song folder are stored relative to it
            const auto trackJson = MBoxStorage::tracksToJson (song, tracksDirectory)["tracks"][0];
            expectEquals (trackJson["file"].toString(), juce::String ("media/backing.wav"));
        }

        beginTest ("Malformed files fail, naming the file");
        {
            MBoxTest::TemporaryDirectory temp;
            MBoxPlaylists playlists;
            MBoxRig rig;
            MBoxBanks banks;

            temp.m_directory.getChildFile ("rig.json").replaceWithText ("{ not json");
            auto result = MBoxStorage (temp.m_directory).load (playlists, rig, banks);
            expect (result.failed());
            expect (result.getErrorMessage().contains ("rig.json"));

            temp.m_directory.getChildFile ("rig.json").replaceWithText ("{\"schemaVersion\": 99}");
            result = MBoxStorage (temp.m_directory).load (playlists, rig, banks);
            expect (result.failed());
            expect (result.getErrorMessage().contains ("schema version"));
        }

        beginTest ("Renames move folders; removals go to the trash");
        {
            MBoxTest::TemporaryDirectory temp;
            const MBoxStorage storage (temp.m_directory);
            MBoxPlaylists playlists;
            MBoxRig rig;
            auto banks = createDefaultBanks();
            buildExample (storage, playlists, rig, banks);
            storage.save (playlists, rig, banks);

            expect (storage.renameSongDirectory ("Tour", "Opener", "First").wasOk());
            expect (storage.getMediaDirectory ("Tour", "First").getChildFile ("backing.wav").existsAsFile());
            expect (! storage.getSongDirectory ("Tour", "Opener").exists());
            expect (storage.renameSongDirectory ("Tour", "First", "Closer").failed());  // already exists

            expect (storage.trashSongDirectory ("Tour", "First").wasOk());
            expect (! storage.getSongDirectory ("Tour", "First").exists());
            expectEquals (temp.m_directory.getChildFile (".trash").getNumberOfChildFiles (juce::File::findDirectories), 1);

            // Folders that were never saved
            expect (storage.renamePlaylistDirectory ("Never saved", "Still not").wasOk());
        }
    }

private:
    static juce::String toJson (const juce::var& value)  { return juce::JSON::toString (value); }

    // A playlist with every kind of track, mixes with an insert, and changed rig and banks
    void buildExample (const MBoxStorage& storage, MBoxPlaylists& playlists, MBoxRig& rig, MBoxBanks& banks)
    {
        const auto wav = storage.getMediaDirectory ("Tour", "Opener").getChildFile ("backing.wav");
        expect (MBoxTest::writeWav (wav, 44100, [] (auto) { return std::pair { 0.1f, -0.1f }; }).wasOk());

        auto playlist = std::make_unique<MBoxPlaylist> ("Tour");
        playlist->addSong ("Opener");
        playlist->addSong ("Closer");
        auto& song = *playlist->getSongInstance ("Opener");

        int audioId = 0, guitarId = 0, infoId = 0;
        song.addTrack (std::make_unique<MBoxAudioTrack> (0, "Backing"), &audioId);
        song.addTrack (std::make_unique<MBoxPhysicalTrack> (0, "Guitar"), &guitarId);
        song.addTrack (std::make_unique<MBoxInfoTrack> (0, "Cues"), &infoId);
        expect (song.setAudioFile (audioId, wav).wasOk());
        expect (song.setAudioStart (audioId, { 2, 1, 1, 0 }).wasOk());

        auto& guitar = *dynamic_cast<MBoxPhysicalTrack*> (song.getTrack (guitarId));
        guitar.setPhysicalInput (1);
        guitar.setDIOutput (6);
        guitar.setBankSlotChange ({ 1, 1, 1, 0 }, { 1, 2 });
        guitar.setBankSlotChange ({ 17, 3, 1, 0 }, { 5, 5 });
        dynamic_cast<MBoxInfoTrack*> (song.getTrack (infoId))->addInfoClick ({ 8, 1, 1, 0 });

        song.addTempo ({ { 9, 1, 1, 0 }, 140 });
        song.addSignature ({ { 13, 1, 1, 0 }, { 7, 8 } });
        song.setLeftLocator ({ 2, 1, 1, 0 });
        song.setEndSongAction (EndSongAction::Continue);
        song.setEndSongDelay (5);
        song.setMetronomeState (MetronomeState::FourCountdownOnly);

        auto& mix = *song.getTrackMix (2, guitarId);
        mix.m_volume = -6.5;
        mix.m_balance = -0.25;
        mix.m_mute = true;
        mix.m_solo = true;

        MBoxEffect effect;
        effect.m_name = "Delay";
        effect.m_format = "VST3";
        effect.m_manufacturer = "Someone";
        effect.m_identifier = "/Library/Audio/Plug-Ins/VST3/Delay.vst3";
        effect.m_uniqueId = 1234;
        effect.m_version = "1.2";
        effect.m_state = juce::MemoryBlock ("\x01\x02\x00\x03", 4);
        effect.m_bypassed = true;
        mix.m_inserts.push_back (effect);
        getBankSlot (banks, { 3, 4 }).m_effectChain.push_back (effect);
        banks[0].m_name = "Clean";
        rig.m_mixBuses[1].getMasterEffectChain().push_back (effect);

        rig.m_mixBuses[0].rename ("FOH");
        rig.m_mixBuses[1].setOutputs ({ 7, -1 });
        rig.m_mixBuses[2].setMasterVolume (-3.0);
        rig.m_mixBuses[2].setMetronomeVolume (-12.0);
        rig.m_device.m_typeName = "CoreAudio";
        rig.m_device.m_bufferSize = 64;

        playlists.push_back (std::move (playlist));
        playlists.push_back (std::make_unique<MBoxPlaylist> ("Rehearsal"));
    }
};

static MBoxStorageTests storageTests;
