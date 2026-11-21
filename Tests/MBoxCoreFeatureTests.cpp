#include "MBoxTestUtilities.h"
#include "TCPServer/MBoxServer.h"

//==============================================================================
// The API added for the UI (UIPortPlan step 4), through MBoxCoreAudio with the null device:
// channel names, the click per mix, playlist copies, playFrom, meters, effectChanged, and
// their TCP methods
//==============================================================================
class MBoxCoreFeatureTests final : public juce::UnitTest
{
public:
    MBoxCoreFeatureTests() : juce::UnitTest ("MBoxCoreFeatures", "MBox") {}

    void runTest() override
    {
        MBoxTest::TemporaryDirectory temp;
        MBoxCoreOptions options;
        options.m_dataDirectory = temp.m_directory.getFullPathName().toStdString();
        options.m_tcpPort = 0;
        options.m_openAudioDevice = false;

        {
            MBoxCoreAudio core (options);

            beginTest ("The device reports its channel counts and names");
            {
                expect (core.getInitialisationResult().wasOk(), core.getInitialisationResult().m_message);
                const auto device = core.getCurrentDevice();
                expectEquals (device.m_numInputs, 2);
                expectEquals (device.m_numOutputs, numberOfMixBus * 2);
                expectEquals (juce::String (device.m_outputNames[1]), juce::String ("Output 2"));
            }

            beginTest ("The click can be switched off per mix bus");
            {
                expect (core.setMetronomeInMix (2, false).wasOk());
                expect (! core.getRig()->m_mixBuses[1].isMetronomeOn());
                expect (core.getRig()->m_mixBuses[0].isMetronomeOn());
                expect (! core.setMetronomeInMix (4, false).wasOk());
            }

            beginTest ("A playlist copy is independent, files included");
            {
                expect (core.addPlaylist ("Set").wasOk());
                expect (core.addSong ("Set", "First").wasOk());
                auto* song = core.getSongInstance ("Set", "First");
                int trackId = 0;
                expect (song->addTrack (std::make_unique<MBoxAudioTrack> (0, "Backing"), &trackId).wasOk());
                song->setMetronomeState (MetronomeState::On);

                const auto source = temp.m_directory.getChildFile ("source.wav");
                expect (MBoxTest::writeWav (source, 4 * 44100, [] (auto) { return std::pair { 0.25f, 0.25f }; }).wasOk());
                expect (core.importAudioFile ("Set", "First", trackId, source.getFullPathName().toStdString()).wasOk());

                expect (core.copyPlaylist ("Set", "Set copy").wasOk());
                expect (core.getPlaylistNames() == std::vector<std::string> { "Set", "Set copy" });
                expect (! core.copyPlaylist ("Set", "set COPY").wasOk());
                expect (! core.copyPlaylist ("Missing", "Other").wasOk());

                auto* copied = dynamic_cast<MBoxAudioTrack*> (core.getSongInstance ("Set copy", "First")->getTrack (trackId));
                auto* original = dynamic_cast<MBoxAudioTrack*> (core.getSongInstance ("Set", "First")->getTrack (trackId));
                expect (copied != nullptr && original != nullptr);
                expect (copied->getFilename().existsAsFile());
                expect (copied->getFilename().getFullPathName().contains ("Set copy"));
                expect (original->getFilename().getFullPathName().contains ("/Set/"));

                // Changing the copy leaves the original alone
                copied->setName ("Changed");
                expectEquals (original->getName(), juce::String ("Backing"));
            }

            beginTest ("playFrom plays from a position; meters move while playing");
            {
                expect (! core.playFrom ("2.1.1.0").wasOk());   // nothing loaded yet
                expect (core.selectSong ("Set", "First").wasOk());
                expect (MBoxTest::waitUntil ([&] { return ! core.getTransportInfo().m_loading && core.getTransportInfo().m_song == "First"; }));

                expect (! core.playFrom ("nonsense").wasOk());
                expect (! core.playFrom ("1.9.1.0").wasOk());
                expect (core.playFrom ("1.2.1.0").wasOk());
                expect (MBoxTest::waitUntil ([&] { return core.getTransportInfo().m_state == "playing"; }));

                // The null device renders in real time: give it a few blocks
                const auto trackId = core.getSongInstance ("Set", "First")->getTracks().front()->getId();
                float trackPeak = 0.0f, masterPeak = 0.0f;
                expect (MBoxTest::waitUntil ([&]
                {
                    trackPeak = juce::jmax (trackPeak, core.takeTrackPeak (1, trackId));
                    masterPeak = juce::jmax (masterPeak, core.takeMasterPeak (1));
                    return trackPeak > 0.2f && masterPeak > 0.2f;
                }, 3000));
                expectWithinAbsoluteError (trackPeak, 0.25f, 0.01f);
                expectEquals (core.takeTrackPeak (1, 999), 0.0f);
                expectEquals (core.takeTrackPeak (9, trackId), 0.0f);

                expect (core.stop().wasOk());
                expect (MBoxTest::waitUntil ([&] { return core.getTransportInfo().m_state == "stopped"; }));
            }

            beginTest ("effectChanged: needs an existing effect, then marks it for saving");
            {
                MBoxEffectTarget slot;
                slot.m_bank = 1;
                slot.m_slot = 1;
                expect (! core.effectChanged (slot, 0).wasOk());
                expect (core.getRunningPlugins (slot, 0).empty());

                MBoxEffect effect;
                effect.m_name = "Some plugin";
                effect.m_state.append ("abc", 3);
                core.getBanks()[0].m_bankSlots[0].m_effectChain.push_back (effect);
                expect (core.effectChanged (slot, 0).wasOk());
                expect (core.save().wasOk());
            }

            beginTest ("TCP: playFrom and setMetronomeInMix");
            {
                MBoxServer server (core);
                bool hello = true;
                auto request = [&] (const juce::String& line) { return juce::JSON::parse (server.handleRequestLine (line, hello)); };

                expectEquals ((int) request ("{\"id\":1,\"method\":\"setMetronomeInMix\",\"params\":{\"bus\":2}}")["error"]["code"],
                              (int) MBoxComAPI::badParameters);
                expect (request ("{\"id\":2,\"method\":\"setMetronomeInMix\",\"params\":{\"bus\":2,\"on\":true}}").hasProperty ("result"));
                expect (core.getRig()->m_mixBuses[1].isMetronomeOn());
                expect (request ("{\"id\":3,\"method\":\"setMetronomeInMix\",\"params\":{\"bus\":3,\"on\":false}}").hasProperty ("result"));

                expectEquals ((int) request ("{\"id\":4,\"method\":\"playFrom\",\"params\":{}}")["error"]["code"], (int) MBoxComAPI::badParameters);
                expect (request ("{\"id\":5,\"method\":\"playFrom\",\"params\":{\"position\":\"1.3.1.0\"}}").hasProperty ("result"));
                expect (core.stop().wasOk());
            }
        }

        beginTest ("The click per mix and edited effect states are saved");
        {
            MBoxCoreAudio core (options);
            expect (core.getInitialisationResult().wasOk(), core.getInitialisationResult().m_message);
            expect (core.getRig()->m_mixBuses[1].isMetronomeOn());
            expect (! core.getRig()->m_mixBuses[2].isMetronomeOn());
            expectEquals ((int) core.getBanks()[0].m_bankSlots[0].m_effectChain.front().m_state.getSize(), 3);
            expect (core.getPlaylistNames() == std::vector<std::string> { "Set", "Set copy" });
        }
    }
};

static MBoxCoreFeatureTests coreFeatureTests;
