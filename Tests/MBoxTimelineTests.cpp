#include "Model/MBoxTimeline.h"

#include <juce_core/juce_core.h>

//==============================================================================
// The timing model, checked against the worked examples of the design document
//==============================================================================
class MBoxTimelineTests final : public juce::UnitTest
{
public:
    MBoxTimelineTests() : juce::UnitTest ("MBoxTimeline", "MBox") {}

    void runTest() override
    {
        beginTest ("Bar text round trip");
        {
            Bar position;
            expect (Bar::fromString ("12.3.2.239", position));
            expect (position == Bar { 12, 3, 2, 239 });
            expectEquals (juce::String (position.toString()), juce::String ("12.3.2.239"));
            expect (! Bar::fromString ("12.3.2", position));
            expect (! Bar::fromString ("12.3.2.1x", position));
        }

        beginTest ("4/4 at 120 BPM: design document examples");
        {
            const MBoxTimeline timeline ({ { Bar {}, 120 } }, { { Bar {}, { 4, 4 } } });
            expectEquals (timeline.barToTicks ({ 1, 1, 1, 0 }), (std::int64_t) 0);
            expectEquals (timeline.barToTicks ({ 2, 1, 1, 0 }), (std::int64_t) 3840);
            expectEquals (timeline.barToTicks ({ 3, 1, 1, 0 }), (std::int64_t) 7680);
            expectWithinAbsoluteError (timeline.ticksToSeconds (3840), 2.0, 1.0e-12);
            expectWithinAbsoluteError (timeline.ticksToSeconds (7680), 4.0, 1.0e-12);
        }

        beginTest ("Tempo change 120 -> 180 at bar 2");
        {
            const MBoxTimeline timeline ({ { Bar {}, 120 }, { { 2, 1, 1, 0 }, 180 } }, {});
            expectEquals (timeline.barToTicks ({ 3, 1, 1, 0 }), (std::int64_t) 7680);
            expectWithinAbsoluteError (timeline.ticksToSeconds (7680), 2.0 + 4.0 * 60.0 / 180.0, 1.0e-12);
            expectEquals (timeline.getTempoAtTicks (3839), 120);
            expectEquals (timeline.getTempoAtTicks (3840), 180);
        }

        beginTest ("Signature change 4/4 -> 2/4 at bar 2");
        {
            const MBoxTimeline timeline ({ { Bar {}, 120 } }, { { Bar {}, { 4, 4 } }, { { 2, 1, 1, 0 }, { 2, 4 } } });
            expectEquals (timeline.barToTicks ({ 3, 1, 1, 0 }), (std::int64_t) 5760);
            expectWithinAbsoluteError (timeline.ticksToSeconds (5760), 3.0, 1.0e-12);
            expect (timeline.ticksToBar (5760) == Bar { 3, 1, 1, 0 });
        }

        beginTest ("Ticks per bar for each signature");
        {
            expectEquals (Signature { 4, 4 }.ticksPerBar(), 3840);
            expectEquals (Signature { 3, 4 }.ticksPerBar(), 2880);
            expectEquals (Signature { 2, 4 }.ticksPerBar(), 1920);
            expectEquals (Signature { 6, 8 }.ticksPerBar(), 2880);
            expectEquals (Signature { 7, 8 }.ticksPerBar(), 3360);
            expectEquals (Signature { 6, 8 }.divisionsPerBeatUnit(), 2);
            expectEquals (Signature { 4, 16 }.divisionsPerBeatUnit(), 1);
            expectEquals (Signature { 2, 2 }.divisionsPerBeatUnit(), 8);
        }

        beginTest ("Frames: rounded once from the origin, no drift");
        {
            const MBoxTimeline timeline;
            // 22.96875 frames per tick at 120 BPM
            expectEquals (timeline.ticksToFrames (1), (std::int64_t) 23);
            expectEquals (timeline.ticksToFrames (32), (std::int64_t) 735);
            // 100 bars of 2 s
            expectEquals (timeline.barToFrames ({ 101, 1, 1, 0 }), (std::int64_t) 8820000);

            // Accumulating individually rounded ticks would drift; the timeline doesn't
            std::int64_t accumulated = 0;

            for (int tick = 0; tick < 3840; ++tick)
                accumulated += timeline.ticksToFrames (tick + 1) - timeline.ticksToFrames (tick);

            expectEquals (accumulated, (std::int64_t) 88200);
        }

        beginTest ("Bar <-> ticks round trip in 3/4, 6/8 and 7/8");
        {
            const MBoxTimeline timeline ({ { Bar {}, 97 } },
                                         { { Bar {}, { 3, 4 } }, { { 5, 1, 1, 0 }, { 6, 8 } }, { { 9, 1, 1, 0 }, { 7, 8 } } });
            juce::Random random (42);
            int checked = 0;

            for (int i = 0; i < 2000; ++i)
            {
                Bar position { 1 + random.nextInt (14), 1 + random.nextInt (7), 1 + random.nextInt (4), random.nextInt (240) };

                if (! timeline.isValidPosition (position))
                    continue;

                ++checked;
                expect (timeline.ticksToBar (timeline.barToTicks (position)) == position, position.toString());
                expect (timeline.framesToBar (timeline.barToFrames (position)) == position, position.toString());
            }

            expectGreaterThan (checked, 100);
            expectEquals (timeline.barToTicks ({ 5, 1, 1, 0 }), (std::int64_t) 4 * 2880);
            expectEquals (timeline.barToTicks ({ 5, 2, 2, 0 }), (std::int64_t) 4 * 2880 + 480 + 240);
        }

        beginTest ("Position validity");
        {
            const MBoxTimeline timeline ({}, { { Bar {}, { 3, 4 } } });
            expect (timeline.isValidPosition ({ 1, 3, 4, 239 }));
            expect (! timeline.isValidPosition ({ 1, 4, 1, 0 }));
            expect (! timeline.isValidPosition ({ 1, 1, 5, 0 }));
            expect (! timeline.isValidPosition ({ 1, 1, 1, 240 }));
            expect (! timeline.isValidPosition ({ 0, 1, 1, 0 }));
        }

        beginTest ("Positions before bar 1 are extrapolated");
        {
            const MBoxTimeline timeline;
            expect (timeline.ticksToBar (-3840) == Bar { 0, 1, 1, 0 });
            expect (timeline.ticksToBar (-1) == Bar { 0, 4, 4, 239 });
            expectEquals (timeline.ticksToFrames (-3840), (std::int64_t) -88200);
        }

        beginTest ("Beat-unit iteration follows signature changes");
        {
            const MBoxTimeline timeline ({}, { { Bar {}, { 6, 8 } }, { { 3, 1, 1, 0 }, { 4, 4 } } });
            std::vector<MBoxTimeline::BeatUnit> beats;
            timeline.forEachBeatUnit (0, timeline.barToTicks ({ 4, 1, 1, 0 }), [&] (const auto& beat) { beats.push_back (beat); });

            expectEquals ((int) beats.size(), 6 + 6 + 4);
            expectEquals (beats[0].beatInBar, 1);
            expectEquals (beats[6].bar, 2);
            expectEquals (beats[6].beatInBar, 1);
            expectEquals (beats[12].bar, 3);
            expectEquals (beats[12].ticks, (std::int64_t) 2 * 2880);
            expectEquals (beats[13].ticks - beats[12].ticks, (std::int64_t) 960);
        }

        beginTest ("Beat-unit length");
        {
            const MBoxTimeline timeline ({ { Bar {}, 120 } }, { { Bar {}, { 6, 8 } } });
            // An eighth note at 120 quarter-note BPM is 0.25 s
            expectWithinAbsoluteError (timeline.getBeatUnitLengthInFrames (0), 11025.0, 1.0e-9);
        }
    }
};

static MBoxTimelineTests timelineTests;
