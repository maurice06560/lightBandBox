#include "MBoxTimeline.h"

#include <algorithm>
#include <cmath>
#include <map>

namespace
{
    // Integer division rounding towards negative infinity
    std::int64_t floorDiv (std::int64_t value, std::int64_t divisor) noexcept
    {
        const auto quotient = value / divisor;
        return (value % divisor != 0 && (value < 0) != (divisor < 0)) ? quotient - 1 : quotient;
    }

    // Seconds taken by a number of ticks at a constant tempo
    double ticksToSecondsAtTempo (double ticks, int bpm) noexcept
    {
        return ticks * 60.0 / ((double) bpm * ticksPerQuarterNote);
    }

    std::int64_t roundToFrame (double frames) noexcept
    {
        return (std::int64_t) std::floor (frames + 0.5);
    }
}

//==============================================================================
// Construction: build the signature segments first, since tempo positions are bars
// and need the signature map to become ticks
//==============================================================================
MBoxTimeline::MBoxTimeline (const std::vector<std::pair<Bar, int>>& tempoGrid,
                            const std::vector<std::pair<Bar, Signature>>& signatureGrid,
                            double sampleRate)
    : m_sampleRate (sampleRate)
{
    std::map<int, Signature> signaturesByBar { { 1, Signature {} } };

    for (const auto& [position, signature] : signatureGrid)
        if (position.m_bar >= 1 && signature.isValid())
            signaturesByBar[position.m_bar] = signature;

    std::int64_t ticks = 0;
    int previousBar = 1;
    Signature previousSignature = signaturesByBar.begin()->second;

    for (const auto& [bar, signature] : signaturesByBar)
    {
        ticks += (std::int64_t) (bar - previousBar) * previousSignature.ticksPerBar();
        m_signatures.push_back ({ bar, ticks, signature });
        previousBar = bar;
        previousSignature = signature;
    }

    std::map<std::int64_t, int> temposByTicks { { 0, defaultTempo } };
    bool hasTempoAtStart = false;

    for (const auto& [position, bpm] : tempoGrid)
    {
        if (bpm < minTempo || bpm > maxTempo || position.m_bar < 1)
            continue;

        const auto tempoTicks = barToTicks (position);
        temposByTicks[tempoTicks] = bpm;
        hasTempoAtStart = hasTempoAtStart || tempoTicks == 0;
    }

    // Without an entry at bar 1, the first tempo of the song also applies before it
    if (! hasTempoAtStart && temposByTicks.size() > 1)
        temposByTicks[0] = std::next (temposByTicks.begin())->second;

    double seconds = 0.0;
    std::int64_t previousTicks = 0;
    int previousBpm = temposByTicks.begin()->second;

    for (const auto& [startTicks, bpm] : temposByTicks)
    {
        seconds += ticksToSecondsAtTempo ((double) (startTicks - previousTicks), previousBpm);
        m_tempos.push_back ({ startTicks, bpm, seconds });
        previousTicks = startTicks;
        previousBpm = bpm;
    }
}

MBoxTimeline::MBoxTimeline()
    : MBoxTimeline ({}, {})
{
}

//==============================================================================
// Segment lookups. The first segment also covers positions before its start.
//==============================================================================
const MBoxTimeline::SignatureSegment& MBoxTimeline::findSignatureSegmentForBar (int bar) const noexcept
{
    auto it = std::upper_bound (m_signatures.begin(), m_signatures.end(), bar,
                                [] (int value, const SignatureSegment& segment) { return value < segment.startBar; });
    return it == m_signatures.begin() ? *it : *std::prev (it);
}

const MBoxTimeline::SignatureSegment& MBoxTimeline::findSignatureSegmentForTicks (std::int64_t ticks) const noexcept
{
    auto it = std::upper_bound (m_signatures.begin(), m_signatures.end(), ticks,
                                [] (std::int64_t value, const SignatureSegment& segment) { return value < segment.startTicks; });
    return it == m_signatures.begin() ? *it : *std::prev (it);
}

const MBoxTimeline::TempoSegment& MBoxTimeline::findTempoSegmentForTicks (double ticks) const noexcept
{
    auto it = std::upper_bound (m_tempos.begin(), m_tempos.end(), ticks,
                                [] (double value, const TempoSegment& segment) { return value < (double) segment.startTicks; });
    return it == m_tempos.begin() ? *it : *std::prev (it);
}

const MBoxTimeline::TempoSegment& MBoxTimeline::findTempoSegmentForSeconds (double seconds) const noexcept
{
    auto it = std::upper_bound (m_tempos.begin(), m_tempos.end(), seconds,
                                [] (double value, const TempoSegment& segment) { return value < segment.startSeconds; });
    return it == m_tempos.begin() ? *it : *std::prev (it);
}

//==============================================================================
Signature MBoxTimeline::getSignatureAtBar (int bar) const noexcept
{
    return findSignatureSegmentForBar (bar).signature;
}

int MBoxTimeline::getTempoAtTicks (std::int64_t ticks) const noexcept
{
    return findTempoSegmentForTicks ((double) ticks).bpm;
}

bool MBoxTimeline::isValidPosition (const Bar& position) const noexcept
{
    const auto signature = getSignatureAtBar (position.m_bar);

    return position.m_bar >= 1
        && position.m_beat >= 1 && position.m_beat <= signature.m_numerator
        && position.m_division >= 1 && position.m_division <= signature.divisionsPerBeatUnit()
        && position.m_tick >= 0 && position.m_tick < ticksPerDivision;
}

//==============================================================================
// Bar <-> ticks
//==============================================================================
std::int64_t MBoxTimeline::barToTicks (const Bar& position) const noexcept
{
    const auto& segment = findSignatureSegmentForBar (position.m_bar);
    const auto& signature = segment.signature;

    return segment.startTicks
         + (std::int64_t) (position.m_bar - segment.startBar) * signature.ticksPerBar()
         + (std::int64_t) (position.m_beat - 1) * signature.ticksPerBeatUnit()
         + (std::int64_t) (position.m_division - 1) * ticksPerDivision
         + position.m_tick;
}

Bar MBoxTimeline::ticksToBar (std::int64_t ticks) const noexcept
{
    const auto& segment = findSignatureSegmentForTicks (ticks);
    const auto ticksPerBar = (std::int64_t) segment.signature.ticksPerBar();
    const auto ticksPerBeatUnit = segment.signature.ticksPerBeatUnit();

    const auto relative = ticks - segment.startTicks;
    const auto barOffset = floorDiv (relative, ticksPerBar);
    const auto withinBar = (int) (relative - barOffset * ticksPerBar);
    const auto withinBeat = withinBar % ticksPerBeatUnit;

    Bar result;
    result.m_bar      = segment.startBar + (int) barOffset;
    result.m_beat     = withinBar / ticksPerBeatUnit + 1;
    result.m_division = withinBeat / ticksPerDivision + 1;
    result.m_tick     = withinBeat % ticksPerDivision;
    return result;
}

//==============================================================================
// Ticks <-> seconds <-> frames
//==============================================================================
double MBoxTimeline::ticksToSeconds (double ticks) const noexcept
{
    const auto& segment = findTempoSegmentForTicks (ticks);
    return segment.startSeconds + ticksToSecondsAtTempo (ticks - (double) segment.startTicks, segment.bpm);
}

double MBoxTimeline::secondsToTicks (double seconds) const noexcept
{
    const auto& segment = findTempoSegmentForSeconds (seconds);
    return (double) segment.startTicks
         + (seconds - segment.startSeconds) * segment.bpm * ticksPerQuarterNote / 60.0;
}

std::int64_t MBoxTimeline::ticksToFrames (std::int64_t ticks) const noexcept
{
    return roundToFrame (ticksToSeconds ((double) ticks) * m_sampleRate);
}

std::int64_t MBoxTimeline::secondsToFrames (double seconds) const noexcept
{
    return roundToFrame (seconds * m_sampleRate);
}

double MBoxTimeline::framesToTicks (std::int64_t frames) const noexcept
{
    return secondsToTicks ((double) frames / m_sampleRate);
}

Bar MBoxTimeline::framesToBar (std::int64_t frames) const noexcept
{
    // The last tick whose (rounded) frame is at or before this frame, so that
    // framesToBar (barToFrames (position)) == position even when the tick's frame was rounded down.
    // Rounding moves a tick by at most half a frame, less than a tick at any supported tempo.
    auto ticks = (std::int64_t) std::floor (framesToTicks (frames));

    if (ticksToFrames (ticks + 1) <= frames)
        ++ticks;
    else if (ticksToFrames (ticks) > frames)
        --ticks;

    return ticksToBar (ticks);
}

double MBoxTimeline::getBeatUnitLengthInFrames (std::int64_t ticks) const noexcept
{
    const auto signature = findSignatureSegmentForTicks (ticks).signature;
    return ticksToSecondsAtTempo (signature.ticksPerBeatUnit(), getTempoAtTicks (ticks)) * m_sampleRate;
}

//==============================================================================
// Beat-unit iteration, segment by segment, so signature changes are respected
//==============================================================================
void MBoxTimeline::forEachBeatUnit (std::int64_t fromTicks, std::int64_t toTicks,
                                    const std::function<void (const BeatUnit&)>& callback) const
{
    for (size_t i = 0; i < m_signatures.size(); ++i)
    {
        const auto& segment = m_signatures[i];
        const bool isLast = i + 1 == m_signatures.size();
        const auto segmentStart = i == 0 ? std::min (segment.startTicks, fromTicks) : segment.startTicks;
        const auto segmentEnd = isLast ? toTicks : std::min (toTicks, m_signatures[i + 1].startTicks);
        const auto beatLength = (std::int64_t) segment.signature.ticksPerBeatUnit();

        if (segmentEnd <= fromTicks || segmentStart >= toTicks)
            continue;

        // First beat boundary at or after fromTicks, counted from the segment's first bar
        auto beatIndex = floorDiv (std::max (fromTicks, segmentStart) - segment.startTicks + beatLength - 1, beatLength);

        for (auto beatTicks = segment.startTicks + beatIndex * beatLength; beatTicks < segmentEnd; beatTicks += beatLength, ++beatIndex)
        {
            const auto barOffset = floorDiv (beatIndex, segment.signature.m_numerator);
            const auto beatInBar = (int) (beatIndex - barOffset * segment.signature.m_numerator) + 1;
            callback ({ beatTicks, segment.startBar + (int) barOffset, beatInBar });
        }
    }
}
