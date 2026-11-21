#pragma once

#include <Model/MBoxSong.h>

//==============================================================================
// The arrangement's view of a song's time: positions in beats (quarter notes, so a constant
// tempo scrolls at a constant speed whatever the signature), built on the library's
// MBoxTimeline (ticks, seconds, frames). The song ends after its last bar (getTotalBar).
//==============================================================================
class TempoMap
{
public:
    explicit TempoMap (const MBoxSong& song)
        : m_timeline (song.getTimeline()), m_totalBars (song.getTotalBar())
    {
    }

    const MBoxTimeline& getTimeline() const noexcept  { return m_timeline; }

    double toBeats (const Bar& position) const            { return (double) m_timeline.barToTicks (position) / ticksPerQuarterNote; }
    double toSeconds (const Bar& position) const          { return m_timeline.ticksToSeconds ((double) m_timeline.barToTicks (position)); }
    double beatsToSeconds (double beats) const            { return m_timeline.ticksToSeconds (beats * ticksPerQuarterNote); }
    double secondsToBeats (double seconds) const          { return m_timeline.secondsToTicks (seconds) / ticksPerQuarterNote; }
    double framesToBeats (std::int64_t frames) const      { return m_timeline.framesToTicks (frames) / ticksPerQuarterNote; }

    // The position at a beat, floored to the tick
    Bar beatsToBar (double beats) const  { return m_timeline.ticksToBar ((std::int64_t) std::floor (beats * ticksPerQuarterNote)); }

    Signature signatureAt (int bar) const  { return m_timeline.getSignatureAtBar (bar); }
    int tempoAt (const Bar& position) const  { return m_timeline.getTempoAtTicks (m_timeline.barToTicks (position)); }

    // Lengths of a bar and of one of its beat-units, in beats
    double barLengthInBeats (int bar) const       { return (double) signatureAt (bar).ticksPerBar() / ticksPerQuarterNote; }
    double beatUnitLengthInBeats (int bar) const  { return (double) signatureAt (bar).ticksPerBeatUnit() / ticksPerQuarterNote; }

    int getTotalBars() const noexcept  { return m_totalBars; }
    // The end of the last bar
    double getLengthInBeats() const    { return toBeats (Bar { m_totalBars + 1, 1, 1, 0 }); }
    double getLengthInSeconds() const  { return toSeconds (Bar { m_totalBars + 1, 1, 1, 0 }); }

private:
    MBoxTimeline m_timeline;
    int m_totalBars;
};
