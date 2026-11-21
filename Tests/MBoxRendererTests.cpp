#include "MBoxTestUtilities.h"
#include "Plugins/MBoxPluginHost.h"

//==============================================================================
// The audio path, rendered offline: clicks, WAVs, locators, fades, song changes, mixing,
// live inputs, routing, latency padding and slot switching. Frames are checked exactly.
//==============================================================================
class MBoxRendererTests final : public juce::UnitTest
{
public:
    MBoxRendererTests() : juce::UnitTest ("MBoxRenderer", "MBox") {}

    void runTest() override
    {
        beginTest ("Test WAVs");   // expect() needs an open test group
        MBoxTest::TemporaryDirectory temp;
        const auto wav = temp.m_directory.getChildFile ("dc.wav");
        expect (MBoxTest::writeWav (wav, 4 * 44100, [] (auto) { return std::pair { 0.5f, -0.25f }; }).wasOk());
        const auto quarter = temp.m_directory.getChildFile ("quarter.wav");
        expect (MBoxTest::writeWav (quarter, 8 * 44100, [] (auto) { return std::pair { 0.25f, 0.25f }; }).wasOk());

        constexpr int beat = 22050;  // a quarter note at 120 BPM
        constexpr int bar = 4 * beat;
        constexpr int fade = 441;

        beginTest ("Count-in and metronome clicks land on exact frames");
        {
            MBoxPlaylist playlist ("P");
            playlist.addSong ("Click");
            playlist.getSongInstance ("Click")->setMetronomeState (MetronomeState::On);
            MBoxRenderer renderer;
            load (renderer, playlist);

            renderer.play();
            const auto output = MBoxTest::render (renderer, 6 * beat + 100);

            // Silent right up to each click's frame, sounding just after it. (The first click
            // is also under the 10 ms play fade-in, so it's checked by its presence only.)
            expectGreaterThan (peak (output, 0, 2000), 0.0f);

            for (int click = 1; click < 6; ++click)
            {
                expectEquals (peak (output, click * beat - 1000, 1000), 0.0f, "before click " + juce::String (click));
                expectGreaterThan (peak (output, click * beat, 30), 0.0f, "click " + juce::String (click));
            }

            // Beat 1 of bar 1 is accented, beat 2 isn't
            expectGreaterThan (peak (output, 4 * beat, 2000), peak (output, 5 * beat, 2000) * 1.2f);
        }

        beginTest ("The metronome state changes live, without a rebuild");
        {
            MBoxPlaylist playlist ("P");
            playlist.addSong ("Click");
            MBoxRenderer renderer;
            load (renderer, playlist);
            auto& song = *renderer.getPublishedPlayer()->m_songs[0];

            // Off: no count-in, no clicks
            renderer.play();
            expectEquals (peak (MBoxTest::render (renderer, 2 * beat), 0, 2 * beat), 0.0f);

            // On while playing: the beats click from the next block (at 2 and 3 beats)
            song.m_metronomeState = MetronomeState::On;
            const auto on = MBoxTest::render (renderer, beat + 100);
            expectGreaterThan (peak (on, 0, 30), 0.0f);
            expectEquals (peak (on, 3000, beat - 4000), 0.0f);
            expectGreaterThan (peak (on, beat, 30), 0.0f);

            // Off again: silent
            song.m_metronomeState = MetronomeState::Off;
            expectEquals (peak (MBoxTest::render (renderer, 2 * beat), 0, 2 * beat), 0.0f);
        }

        beginTest ("WAVs start on their exact frame and are cut outside the locators");
        {
            MBoxPlaylist playlist ("P");
            playlist.addSong ("Song");
            auto& song = *playlist.getSongInstance ("Song");
            addAudioTrack (song, wav, { 2, 1, 1, 0 });
            MBoxRenderer renderer;
            load (renderer, playlist);

            renderer.play();
            const auto output = MBoxTest::render (renderer, bar + 1000);
            expectEquals (MBoxTest::findOnset (output, 0), bar);
            expectEquals (output.getSample (0, bar + 500), 0.5f);
            expectEquals (output.getSample (1, bar + 500), -0.25f);

            // Starting inside the WAV fades in over 10 ms
            expect (song.setLeftLocator ({ 2, 3, 1, 0 }).wasOk());
            MBoxRenderer fromLocator;
            load (fromLocator, playlist);
            fromLocator.play();
            const auto fromMiddle = MBoxTest::render (fromLocator, 1000);
            expectLessThan (std::abs (fromMiddle.getSample (0, 1)), 0.01f);
            expectEquals (fromMiddle.getSample (0, fade + 10), 0.5f);
        }

        beginTest ("With latency, a song's faded end is heard before the next one, and a start plays no stale audio");
        {
            MBoxPlaylist playlist ("P");
            playlist.addSong ("A");
            playlist.addSong ("B");
            auto& a = *playlist.getSongInstance ("A");
            addAudioTrack (a, wav, { 1, 1, 1, 0 });
            expect (a.setRightLocator ({ 2, 1, 1, 0 }).wasOk());   // inside the WAV: it's cut there
            a.setEndSongAction (EndSongAction::Continue);
            a.setEndSongDelay (0);

            // A playlist padded to 1000 frames, as a latent plugin somewhere would make it
            constexpr int latency = 1000;
            const auto banks = createDefaultBanks();
            const MBoxRig rig;
            const MBoxPlayerBuildContext context { m_clips, nullptr, banks, rig, 44100.0, MBoxRenderer::maxChunkSize, 0.0, {}, {} };
            juce::String error;
            auto player = buildPlaylistPlayer (playlist, context, error);
            expect (player != nullptr, error);
            player->m_totalLatency = latency;

            for (auto& song : player->m_songs)
                for (auto& track : song->m_tracks)
                    for (auto& inserts : track.m_inserts)
                        inserts->setCompensatedLatency (latency);

            MBoxRenderer renderer;
            renderer.publish (std::move (player));
            MBoxTest::render (renderer, 1);

            renderer.play();
            const auto output = MBoxTest::render (renderer, bar + latency + 2000);

            // Heard latency frames late, faded out up to the right locator, then song B
            expectEquals (output.getSample (0, latency + 1000), 0.5f);
            expectEquals (output.getSample (0, bar + latency - 500), 0.5f, "the end still in the delay is heard");
            expectLessThan (std::abs (output.getSample (0, bar + latency - 1)), 0.01f);
            expectEquals (peak (output, bar + latency, 2000), 0.0f);
            expectEquals (renderer.getStatus().m_songIndex, 1);

            float largestStep = 0.0f;

            for (int i = 1; i < output.getNumSamples(); ++i)
                largestStep = juce::jmax (largestStep, std::abs (output.getSample (0, i) - output.getSample (0, i - 1)));

            expectLessThan (largestStep, 0.01f, "no jump in the signal");

            // Stopped in the middle of A, then played again: the delays held A's audio
            renderer.stop();
            MBoxTest::render (renderer, 2000);
            renderer.selectSong (0);
            MBoxTest::render (renderer, 1);
            expectEquals (renderer.getStatus().m_songIndex, 0);
            renderer.play();
            expectEquals (MBoxTest::render (renderer, bar / 2).getSample (0, bar / 2 - 1), 0.5f);
            renderer.stop();
            MBoxTest::render (renderer, 2000);
            renderer.play();
            const auto restart = MBoxTest::render (renderer, latency + 1000);
            expectLessThan (peak (restart, 0, latency), 0.01f, "no stale audio at the start");
        }

        beginTest ("Pause fades out and keeps the position; play resumes from it");
        {
            MBoxPlaylist playlist ("P");
            playlist.addSong ("Song");
            addAudioTrack (*playlist.getSongInstance ("Song"), wav, { 1, 1, 1, 0 });
            MBoxRenderer renderer;
            load (renderer, playlist);

            renderer.play();
            MBoxTest::render (renderer, beat);
            renderer.pause();
            const auto fadingOut = MBoxTest::render (renderer, 1024);
            expectEquals (fadingOut.getSample (0, fade + 5), 0.0f);
            expect (renderer.getStatus().m_state == TransportState::Paused);

            const auto pausedAt = renderer.getStatus().m_position;
            MBoxTest::render (renderer, 1024);
            expectEquals (renderer.getStatus().m_position, pausedAt);

            renderer.play();
            const auto resumed = MBoxTest::render (renderer, 1024);
            expectEquals (resumed.getSample (0, fade + 5), 0.5f);
            expectEquals (renderer.getStatus().m_position, pausedAt + 1024);
        }

        beginTest ("Stop returns to the left locator");
        {
            MBoxPlaylist playlist ("P");
            playlist.addSong ("Song");
            playlist.getSongInstance ("Song")->setLeftLocator ({ 3, 1, 1, 0 });
            MBoxRenderer renderer;
            load (renderer, playlist);

            renderer.play();
            MBoxTest::render (renderer, beat);
            renderer.stop();
            MBoxTest::render (renderer, 1024);
            expect (renderer.getStatus().m_state == TransportState::Stopped);
            expectEquals (renderer.getStatus().m_position, (std::int64_t) 2 * bar);
        }

        beginTest ("A song ending with Stop stops at its right locator");
        {
            MBoxPlaylist playlist ("P");
            playlist.addSong ("Short");
            playlist.addSong ("Next");
            expect (playlist.getSongInstance ("Short")->setRightLocator ({ 2, 1, 1, 0 }).wasOk());
            MBoxRenderer renderer;
            load (renderer, playlist);

            renderer.play();
            MBoxTest::render (renderer, bar + 1024);
            expect (renderer.getStatus().m_state == TransportState::Stopped);
            expectEquals (renderer.getStatus().m_songIndex, 0);
            expectEquals (renderer.getStatus().m_position, (std::int64_t) 0);
        }

        beginTest ("Continue: end delay, then the next song's count-in, then silence");
        {
            MBoxPlaylist playlist ("P");
            playlist.addSong ("A");
            playlist.addSong ("B");
            auto& a = *playlist.getSongInstance ("A");
            expect (a.setRightLocator ({ 2, 1, 1, 0 }).wasOk());
            a.setEndSongAction (EndSongAction::Continue);
            a.setEndSongDelay (1);
            playlist.getSongInstance ("B")->setMetronomeState (MetronomeState::FourCountdownOnly);
            MBoxRenderer renderer;
            load (renderer, playlist);

            renderer.play();
            const auto output = MBoxTest::render (renderer, bar + 44100 + bar + beat);
            const auto countInStart = bar + 44100;

            expectEquals (MBoxTest::findOnset (output, 0), countInStart + 1);
            expectEquals (renderer.getStatus().m_songIndex, 1);
            expect (renderer.getStatus().m_state == TransportState::Playing);

            for (int click = 1; click < 4; ++click)
                expectGreaterThan (peak (output, countInStart + click * beat, 2000), 0.01f);

            // FourCountdownOnly: no clicks once the song has started
            expectEquals (peak (output, countInStart + bar + 2300, beat - 2400), 0.0f);
        }

        beginTest ("Volume, mute and solo are smooth and per bus");
        {
            MBoxPlaylist playlist ("P");
            playlist.addSong ("Song");
            auto& song = *playlist.getSongInstance ("Song");
            addAudioTrack (song, quarter, { 1, 1, 1, 0 });
            addAudioTrack (song, quarter, { 1, 1, 1, 0 });
            MBoxRenderer renderer;
            auto* player = load (renderer, playlist);
            auto& first = player->m_songs[0]->m_tracks[0];
            auto& second = player->m_songs[0]->m_tracks[1];

            renderer.play();
            expectEquals (MBoxTest::render (renderer, 1024).getSample (0, 1000), 0.5f);

            first.m_mix[0]->m_solo = true;
            auto output = MBoxTest::render (renderer, 1024);
            expectEquals (output.getSample (0, 1000), 0.25f);
            // Smooth: no jump in the first samples
            expectGreaterThan (output.getSample (0, 0), 0.45f);
            // Other buses aren't affected
            expectEquals (output.getSample (2, 1000), 0.5f);

            first.m_mix[0]->m_mute = true;
            expectEquals (MBoxTest::render (renderer, 1024).getSample (0, 1000), 0.0f);

            first.m_mix[0]->m_mute = false;
            first.m_mix[0]->m_solo = false;
            second.m_mix[0]->m_gain = volumeDbToGain (-6.0206);
            expectWithinAbsoluteError (MBoxTest::render (renderer, 1024).getSample (0, 1000), 0.375f, 1.0e-4f);
        }

        beginTest ("playFrom starts anywhere, with the count-in only at the left locator, and jumps while playing");
        {
            MBoxPlaylist playlist ("P");
            playlist.addSong ("Song");
            auto& song = *playlist.getSongInstance ("Song");
            song.setMetronomeState (MetronomeState::FourCountdownOnly);
            addAudioTrack (song, wav, { 2, 1, 1, 0 });
            MBoxRenderer renderer;
            load (renderer, playlist);

            // Inside the WAV: sound at once (after the 10 ms fade-in), no count-in
            renderer.playFrom (bar + beat);
            auto output = MBoxTest::render (renderer, 2000);
            expectEquals (output.getSample (0, fade + 10), 0.5f);
            expectEquals (renderer.getStatus().m_position, (std::int64_t) (bar + beat + 2000));

            // While playing: fades out, then jumps back to bar 1 (before the WAV: silence)
            renderer.playFrom (bar / 2);
            output = MBoxTest::render (renderer, 2 * fade);
            expectLessThan (std::abs (output.getSample (0, fade + 10)), 0.01f);
            expect (renderer.getStatus().m_state == TransportState::Playing);
            expectLessThan (renderer.getStatus().m_position, (std::int64_t) bar);

            // From the left locator: the count-in plays first
            renderer.stop();
            MBoxTest::render (renderer, 1024);
            renderer.playFrom (0);
            MBoxTest::render (renderer, 100);
            expectLessThan (renderer.getStatus().m_position, (std::int64_t) 0);
        }

        beginTest ("Meters: track, master and metronome peaks, reset when taken; the click per bus");
        {
            MBoxPlaylist playlist ("P");
            playlist.addSong ("Song");
            auto& song = *playlist.getSongInstance ("Song");
            song.setMetronomeState (MetronomeState::On);
            addAudioTrack (song, quarter, { 1, 1, 1, 0 });
            MBoxRenderer renderer;
            auto* player = load (renderer, playlist);
            auto& mix = *player->m_songs[0]->m_tracks[0].m_mix[0];

            expectEquals (renderer.takeMasterPeak (1), 0.0f);
            renderer.play();
            MBoxTest::render (renderer, 4 * beat + 2048);
            expectWithinAbsoluteError (mix.takePeak(), 0.25f, 1.0e-4f);
            expectEquals (mix.takePeak(), 0.0f);
            expectGreaterThan (renderer.takeMasterPeak (1), 0.25f);
            expectGreaterThan (renderer.takeMetronomePeak (1), 0.0f);
            expectEquals (renderer.takeMetronomePeak (1), 0.0f);

            // The click switched off in bus 1 only
            renderer.setBus (1, { 0, 1 }, 0.0, 0.0, 0.0, false);
            mix.m_mute = true;
            for (int bus = 1; bus <= numberOfMixBus; ++bus)
                renderer.takeMetronomePeak (bus);

            const auto output = MBoxTest::render (renderer, 2 * bar);
            expectEquals (renderer.takeMetronomePeak (1), 0.0f);
            expectGreaterThan (renderer.takeMetronomePeak (2), 0.0f);
            expectGreaterThan (peak (output, 0, 2 * bar), -1.0f);
            expectEquals (output.getMagnitude (0, 1024, 2 * bar - 1024), 0.0f);   // bus 1: muted track, no click
        }

        beginTest ("A single-song rebuild shares all but the edited song, and keeps its running parts");
        {
            MBoxPlaylist playlist ("P");
            playlist.addSong ("A");
            playlist.addSong ("B");
            auto& a = *playlist.getSongInstance ("A");
            addAudioTrack (a, wav, { 1, 1, 1, 0 });
            auto physical = std::make_unique<MBoxPhysicalTrack> (0, "Guitar");
            physical->setPhysicalInput (1);
            physical->setBankSlotChange ({ 1, 1, 1, 0 }, { 1, 1 });
            int guitarId = 0;
            expect (a.addTrack (std::move (physical), &guitarId).wasOk());

            MBoxRenderer renderer;
            auto* running = load (renderer, playlist);
            const auto banks = createDefaultBanks();
            const MBoxRig rig;
            const MBoxPlayerBuildContext context { m_clips, nullptr, banks, rig, 44100.0, MBoxRenderer::maxChunkSize, 0.0, {}, {} };
            juce::String error;

            // A cue track added to A
            auto cues = std::make_unique<MBoxInfoTrack> (0, "Cues");
            cues->addInfoClick ({ 2, 1, 1, 0 });
            int cuesId = 0;
            expect (a.addTrack (std::move (cues), &cuesId).wasOk());

            auto rebuilt = rebuildSongPlayer (playlist, "A", *running, context, error);
            expect (rebuilt != nullptr, error);
            expect (rebuilt->m_songs[1] == running->m_songs[1]);
            expect (rebuilt->m_songs[0] != running->m_songs[0]);
            expect (rebuilt->m_liveInputs == running->m_liveInputs);
            expect (rebuilt->m_masterChains == running->m_masterChains);

            const auto& before = *running->m_songs[0];
            auto& after = *rebuilt->m_songs[0];
            expectEquals ((int) after.m_tracks.size(), 3);
            expect (after.m_tracks[0].m_inserts[0] == before.m_tracks[0].m_inserts[0]);
            expect (after.m_tracks[0].m_mix[0] == before.m_tracks[0].m_mix[0]);
            expect (after.m_tracks[1].m_liveInput == before.m_tracks[1].m_liveInput);
            expectEquals ((int) after.findTrack (cuesId)->m_infoClicks.size(), 4);

            // It plays
            renderer.publish (std::move (rebuilt));
            renderer.play();
            MBoxTest::render (renderer, 1024);
            renderer.collectGarbage();
            running = renderer.getPublishedPlayer();

            // A cue moved, and a track removed: also rebuilt alone
            dynamic_cast<MBoxInfoTrack*> (a.getTrack (cuesId))->addInfoClick ({ 3, 1, 1, 0 });
            expect (a.removeTrack (after.m_tracks[0].m_trackId).wasOk());
            rebuilt = rebuildSongPlayer (playlist, "A", *running, context, error);
            expect (rebuilt != nullptr, error);
            expectEquals ((int) rebuilt->m_songs[0]->m_tracks.size(), 2);
            expectEquals ((int) rebuilt->m_songs[0]->findTrack (cuesId)->m_infoClicks.size(), 8);

            // A slot the live input doesn't run yet, or a new song: only a full build can
            dynamic_cast<MBoxPhysicalTrack*> (a.getTrack (guitarId))->setBankSlotChange ({ 2, 1, 1, 0 }, { 1, 2 });
            expect (rebuildSongPlayer (playlist, "A", *running, context, error) == nullptr);
            expect (error.isEmpty(), error);
            expect (dynamic_cast<MBoxPhysicalTrack*> (a.getTrack (guitarId))->removeBankSlotChange ({ 2, 1, 1, 0 }));

            playlist.addSong ("C");
            expect (rebuildSongPlayer (playlist, "A", *running, context, error) == nullptr);
            expect (error.isEmpty(), error);
        }

        beginTest ("Live inputs: pan, DI output, mono bus, master volume, silent when stopped");
        {
            MBoxPlaylist playlist ("P");
            playlist.addSong ("Song");
            auto& song = *playlist.getSongInstance ("Song");
            auto physical = std::make_unique<MBoxPhysicalTrack> (0, "Guitar");
            physical->setPhysicalInput (1);
            physical->setDIOutput (6);
            song.addTrack (std::move (physical));

            MBoxRenderer renderer;
            load (renderer, playlist);
            renderer.setBus (2, { 2, -1 }, 0.0, 0.0, 0.0);
            renderer.setBus (3, { 4, 5 }, -6.0206, 0.0, 0.0);

            expectEquals (peak (MBoxTest::render (renderer, 1024, 8, 0.5f), 0, 1024), 0.0f);

            renderer.play();
            const auto output = MBoxTest::render (renderer, 1024, 8, 0.5f);
            const auto centre = 0.5f * 0.70710678f;
            expectWithinAbsoluteError (output.getSample (0, 1000), centre, 1.0e-5f);
            expectWithinAbsoluteError (output.getSample (1, 1000), centre, 1.0e-5f);
            expectWithinAbsoluteError (output.getSample (2, 1000), 0.5f, 1.0e-5f);   // mono bus
            expectEquals (output.getSample (3, 1000), 0.0f);
            expectWithinAbsoluteError (output.getSample (4, 1000), centre * 0.5f, 1.0e-4f);
            expectEquals (output.getSample (6, 1000), 0.5f);                          // DI
        }

        beginTest ("Unloading while playing stops and silences");
        {
            MBoxPlaylist playlist ("P");
            playlist.addSong ("Song");
            addAudioTrack (*playlist.getSongInstance ("Song"), wav, { 1, 1, 1, 0 });
            MBoxRenderer renderer;
            load (renderer, playlist);

            renderer.play();
            expectEquals (MBoxTest::render (renderer, 1024).getSample (0, 1000), 0.5f);

            renderer.publish (nullptr);
            const auto output = MBoxTest::render (renderer, 1024);
            expectEquals (peak (output, 0, 1024), 0.0f);
            expect (renderer.getStatus().m_state == TransportState::Stopped);
            renderer.collectGarbage();
            expect (renderer.getPublishedPlayer() != nullptr && renderer.getPublishedPlayer()->m_songs.empty());
        }

        beginTest ("Effect chains pad themselves to a common latency");
        {
            MBoxEffectChain chain (1);
            chain.prepare (44100.0, 256);
            chain.setCompensatedLatency (100);

            juce::AudioBuffer<float> buffer (1, 256);
            buffer.clear();
            buffer.setSample (0, 0, 1.0f);
            chain.process (buffer.getArrayOfWritePointers(), 64);
            expectEquals (peak (buffer, 0, 64), 0.0f);

            buffer.clear();
            chain.process (buffer.getArrayOfWritePointers(), 64);
            expectEquals (buffer.getSample (0, 36), 1.0f);
        }

        beginTest ("Switching slots crossfades without a level change");
        {
            MBoxLiveInput input;
            input.addSlotChain ({ 1, 1 }, std::make_unique<MBoxEffectChain> (1));
            input.addSlotChain ({ 1, 2 }, std::make_unique<MBoxEffectChain> (1));
            input.prepare (44100.0, 256, 0);

            std::vector<float> samples (256);
            float minimum = 1.0f, maximum = 0.0f;

            for (int block = 0; block < 20; ++block)
            {
                std::fill (samples.begin(), samples.end(), 1.0f);
                input.process (samples.data(), 256, MBoxSlotRef { 1, block < 10 ? 1 : 2 });
                minimum = juce::jmin (minimum, *std::min_element (samples.begin(), samples.end()));
                maximum = juce::jmax (maximum, *std::max_element (samples.begin(), samples.end()));
            }

            expectWithinAbsoluteError (minimum, 1.0f, 1.0e-5f);
            expectWithinAbsoluteError (maximum, 1.0f, 1.0e-5f);
        }

       #if JUCE_MAC
        beginTest ("Hosting a real plugin (Apple AUDelay) in a mono chain");
        {
            MBoxTest::TemporaryDirectory pluginTemp;
            MBoxPluginHost host (pluginTemp.m_directory.getChildFile ("plugins.xml"));
            MBoxEffect delay;
            delay.m_name = "AUDelay";
            delay.m_format = "AudioUnit";
            delay.m_manufacturer = "Apple";
            delay.m_identifier = "AudioUnit:Effects/aufx,dely,appl";

            juce::String error;
            auto chain = host.createChain ({ delay }, 1, 44100.0, 256, error);
            expect (chain != nullptr, error);

            if (chain != nullptr)
            {
                chain->prepare (44100.0, 256);
                chain->setCompensatedLatency (chain->getLatencySamples());
                chain->warmUp (0.5);

                juce::AudioBuffer<float> buffer (1, 256);
                float energy = 0.0f;

                for (int block = 0; block < 200; ++block)
                {
                    buffer.clear();

                    if (block == 0)
                        buffer.setSample (0, 0, 1.0f);

                    chain->process (buffer.getArrayOfWritePointers(), 256);
                    energy += buffer.getMagnitude (0, 0, 256);
                }

                expectGreaterThan (energy, 0.0f);
            }
        }
       #endif
    }

private:
    // Builds a playlist player (no plugins) and hands it to the renderer
    MBoxPlaylistPlayer* load (MBoxRenderer& renderer, const MBoxPlaylist& playlist)
    {
        const auto banks = createDefaultBanks();
        const MBoxRig rig;
        const MBoxPlayerBuildContext context { m_clips, nullptr, banks, rig, 44100.0, MBoxRenderer::maxChunkSize, 0.0, {}, {} };
        juce::String error;
        auto player = buildPlaylistPlayer (playlist, context, error);
        expect (player != nullptr, error);

        auto* raw = player.get();
        renderer.publish (std::move (player));
        MBoxTest::render (renderer, 1);  // the audio thread takes the player at the start of a block
        renderer.collectGarbage();
        return raw;
    }

    void addAudioTrack (MBoxSong& song, const juce::File& file, const Bar& start)
    {
        int id = 0;
        expect (song.addTrack (std::make_unique<MBoxAudioTrack> (0, "Backing"), &id).wasOk());
        expect (song.setAudioFile (id, file).wasOk());
        expect (song.setAudioStart (id, start).wasOk());
    }

    static float peak (const juce::AudioBuffer<float>& buffer, int start, int length)
    {
        return buffer.getMagnitude (0, start, juce::jmin (length, buffer.getNumSamples() - start));
    }

    MBoxAudioClipCache m_clips;
};

static MBoxRendererTests rendererTests;
