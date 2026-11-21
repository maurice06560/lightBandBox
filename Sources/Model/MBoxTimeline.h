#pragma once

#include "MBoxTypes.h"

#include <functional>
#include <utility>
#include <vector>

//==============================================================================
// Converts between the representations of a position in a song:
//
//   Bar {bar, beat, division, tick}  <->  absolute ticks  <->  seconds  <->  sample frames
//
// - Bar <-> ticks uses the signature map and the 960 PPQ convention. A signature change
//   alters the length of the following bars; a tempo change doesn't.
// - Ticks <-> seconds integrates the tempo map. BPM always counts quarter notes, and tempo
//   changes are instantaneous.
// - Seconds <-> frames uses the sample rate. A frame position is computed from the start of
//   the song and rounded once (to the nearest frame, halves rounded up), so no error builds up.
//
// Tick 0, second 0 and frame 0 are all the start of bar 1. Positions before bar 1 are
// extrapolated with the first signature and the first tempo.
// The object is immutable once built, so it can be shared with the audio thread.
//==============================================================================
class MBoxTimeline
{
public:
    // A beat-unit boundary, as reported by forEachBeatUnit
    struct BeatUnit
    {
        std::int64_t ticks;
        int bar;
        int beatInBar;  // 1-based
    };

    // Builds the maps from a song's grids. Grids don't need to be sorted. An entry for a
    // position already in the grid replaces the earlier one. Signature entries use only their
    // bar number. When bar 1 has no entry, 4/4 and defaultTempo are assumed from bar 1.
    MBoxTimeline (const std::vector<std::pair<Bar, int>>& tempoGrid,
                  const std::vector<std::pair<Bar, Signature>>& signatureGrid,
                  double sampleRate = supportedSampleRate);

    // A 4/4, defaultTempo timeline
    MBoxTimeline();

    Signature getSignatureAtBar (int bar) const noexcept;
    int getTempoAtTicks (std::int64_t ticks) const noexcept;
    double getSampleRate() const noexcept  { return m_sampleRate; }

    // True when the beat, division and tick fields are within range for the signature of
    // that bar. Bar numbers below 1 are rejected.
    bool isValidPosition (const Bar& position) const noexcept;

    std::int64_t barToTicks (const Bar& position) const noexcept;
    Bar ticksToBar (std::int64_t ticks) const noexcept;

    double ticksToSeconds (double ticks) const noexcept;
    double secondsToTicks (double seconds) const noexcept;

    // Rounded once, to the nearest frame
    std::int64_t ticksToFrames (std::int64_t ticks) const noexcept;
    std::int64_t barToFrames (const Bar& position) const noexcept  { return ticksToFrames (barToTicks (position)); }
    std::int64_t secondsToFrames (double seconds) const noexcept;

    // Fractional ticks at a frame. Display positions are the floor of this.
    double framesToTicks (std::int64_t frames) const noexcept;
    Bar framesToBar (std::int64_t frames) const noexcept;

    // Length of one beat-unit, in frames, at the given tick position (signature and tempo there)
    double getBeatUnitLengthInFrames (std::int64_t ticks) const noexcept;

    // Calls the callback, in order, for every beat-unit boundary b with fromTicks <= b < toTicks
    void forEachBeatUnit (std::int64_t fromTicks, std::int64_t toTicks,
                          const std::function<void (const BeatUnit&)>& callback) const;

private:
    struct SignatureSegment
    {
        int startBar;
        std::int64_t startTicks;
        Signature signature;
    };

    struct TempoSegment
    {
        std::int64_t startTicks;
        int bpm;
        double startSeconds;
    };

    const SignatureSegment& findSignatureSegmentForBar (int bar) const noexcept;
    const SignatureSegment& findSignatureSegmentForTicks (std::int64_t ticks) const noexcept;
    const TempoSegment& findTempoSegmentForTicks (double ticks) const noexcept;
    const TempoSegment& findTempoSegmentForSeconds (double seconds) const noexcept;

    std::vector<SignatureSegment> m_signatures;  // sorted, the first one starts at bar 1
    std::vector<TempoSegment> m_tempos;          // sorted, the first one starts at tick 0
    double m_sampleRate { supportedSampleRate };
};
