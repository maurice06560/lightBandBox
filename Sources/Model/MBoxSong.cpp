#include "MBoxSong.h"
#include "MBoxAudioTrack.h"

#include <juce_audio_formats/juce_audio_formats.h>

#include <algorithm>
#include <cmath>

void MBoxSong::addAudioTrack (const Bar& startAudio, const juce::File& audioFile)
{
    m_tracks.push_back (std::make_unique<MBoxAudioTrack> (startAudio, audioFile.getFileName()));

    // Read the file's header for its length
    juce::AudioFormatManager formatManager;
    formatManager.registerBasicFormats();

    const std::unique_ptr<juce::AudioFormatReader> reader (formatManager.createReaderFor (audioFile));

    if (reader == nullptr || reader->sampleRate <= 0.0)
    {
        juce::Logger::writeToLog ("MBoxSong: cannot read the length of audio file: " + audioFile.getFullPathName());
        return;
    }

    const auto lengthSeconds = (double) reader->lengthInSamples / reader->sampleRate;

    // Round up, so the end position covers the last sample of the file
    const auto startSeconds = convertTickToSeconds ((double) convertBarToTick (startAudio));
    const auto endTick      = (std::int64_t) std::ceil (convertSecondsToTick (startSeconds + lengthSeconds));
    const auto audioEnd     = convertTickToBar (endTick);

    if (audioEnd > m_songEndPosition)
        m_songEndPosition = audioEnd;
}

void MBoxSong::addPhysicalTrack (int physicalInput)
{
    m_tracks.push_back (std::make_unique<MBoxPhysicalTrack> (physicalInput));
}

//==============================================================================
// Position conversions
//==============================================================================
std::int64_t MBoxSong::convertBarToTick (const Bar& bar) const
{
    // The grid always starts with a signature at bar 1, so it is never empty
    jassert (! m_signatureGrid.empty() && bar.bar >= 1);

    std::int64_t ticks = 0;

    for (size_t i = 0; i < m_signatureGrid.size(); ++i)
    {
        const auto ticksPerBar = (std::int64_t) m_signatureGrid[i].second.numerator * Bar::ticksPerBeat;
        const auto startBar    = m_signatureGrid[i].first.bar;
        const auto isLast      = i + 1 == m_signatureGrid.size();

        // This signature lasts until the bar where the next one starts
        if (isLast || bar.bar < m_signatureGrid[i + 1].first.bar)
        {
            ticks += (std::int64_t) (bar.bar - startBar) * ticksPerBar;
            break;
        }

        ticks += (std::int64_t) (m_signatureGrid[i + 1].first.bar - startBar) * ticksPerBar;
    }

    // Position inside the bar
    return ticks
         + (std::int64_t) (bar.beat - 1) * Bar::ticksPerBeat
         + (std::int64_t) (bar.division - 1) * Bar::ticksPerDivision
         + bar.tick;
}

Bar MBoxSong::convertTickToBar (std::int64_t ticks) const
{
    jassert (! m_signatureGrid.empty());

    ticks = std::max<std::int64_t> (0, ticks);

    std::int64_t segmentStart = 0;      // Tick where signature i starts

    for (size_t i = 0; i < m_signatureGrid.size(); ++i)
    {
        const auto ticksPerBar = (std::int64_t) m_signatureGrid[i].second.numerator * Bar::ticksPerBeat;
        const auto startBar    = m_signatureGrid[i].first.bar;

        if (i + 1 < m_signatureGrid.size())
        {
            const auto nextStart = segmentStart + (std::int64_t) (m_signatureGrid[i + 1].first.bar - startBar) * ticksPerBar;

            if (ticks >= nextStart)
            {
                segmentStart = nextStart;
                continue;
            }
        }

        const auto relative = ticks - segmentStart;
        const auto inBar    = relative % ticksPerBar;
        const auto inBeat   = inBar % Bar::ticksPerBeat;

        return { startBar + (int) (relative / ticksPerBar),
                 (int) (inBar / Bar::ticksPerBeat) + 1,
                 (int) (inBeat / Bar::ticksPerDivision) + 1,
                 (int) (inBeat % Bar::ticksPerDivision) };
    }

    return {};
}

double MBoxSong::convertTickToSeconds (double ticks) const
{
    // The grid always starts with a tempo at {1,1,1,0}, so it is never empty
    jassert (! m_tempoGrid.empty());

    double seconds = 0.0;

    for (size_t i = 0; i < m_tempoGrid.size(); ++i)
    {
        const auto secondsPerTick = 60.0 / ((double) m_tempoGrid[i].second * Bar::ticksPerBeat);
        const auto startTick      = (double) convertBarToTick (m_tempoGrid[i].first);

        // This tempo lasts until the tick where the next one starts
        if (i + 1 == m_tempoGrid.size())
            return seconds + (ticks - startTick) * secondsPerTick;

        const auto endTick = (double) convertBarToTick (m_tempoGrid[i + 1].first);

        if (ticks < endTick)
            return seconds + (ticks - startTick) * secondsPerTick;

        seconds += (endTick - startTick) * secondsPerTick;
    }

    return seconds;
}

double MBoxSong::convertSecondsToTick (double seconds) const
{
    jassert (! m_tempoGrid.empty());

    double segmentStartSeconds = 0.0;

    for (size_t i = 0; i < m_tempoGrid.size(); ++i)
    {
        const auto secondsPerTick = 60.0 / ((double) m_tempoGrid[i].second * Bar::ticksPerBeat);
        const auto startTick      = (double) convertBarToTick (m_tempoGrid[i].first);

        if (i + 1 == m_tempoGrid.size())
            return startTick + (seconds - segmentStartSeconds) / secondsPerTick;

        const auto endTick         = (double) convertBarToTick (m_tempoGrid[i + 1].first);
        const auto segmentSeconds  = (endTick - startTick) * secondsPerTick;

        if (seconds < segmentStartSeconds + segmentSeconds)
            return startTick + (seconds - segmentStartSeconds) / secondsPerTick;

        segmentStartSeconds += segmentSeconds;
    }

    return 0.0;
}

std::vector<MBoxTrack*> MBoxSong::getTracks() const
{
    std::vector<MBoxTrack*> tracks;
    tracks.reserve (m_tracks.size());

    for (const auto& track : m_tracks)
        tracks.push_back (track.get());

    return tracks;
}

namespace
{
    // Inserts value at bar in a grid kept sorted by position, or replaces the value already at that position.
    template <typename Value>
    void setGridValue (std::vector<std::pair<Bar, Value>>& grid, const Bar& bar, const Value& value)
    {
        const auto it = std::lower_bound (grid.begin(), grid.end(), bar,
                                          [] (const auto& entry, const Bar& position) { return entry.first < position; });

        if (it != grid.end() && it->first == bar)
            it->second = value;
        else
            grid.insert (it, { bar, value });
    }
}

void MBoxSong::addTempo (const Bar& bar, int tempo)
{
    jassert (tempo > 0);

    if (tempo <= 0)
        return;

    setGridValue (m_tempoGrid, bar, tempo);
}

void MBoxSong::addSignature (const Bar& bar, const Signature& signature)
{
    jassert (signature.isValid());

    if (! signature.isValid())
        return;

    setGridValue (m_signatureGrid, bar, signature);
}

int MBoxSong::getTempoForBar (const Bar& bar) const
{
    // The grid always starts with a tempo at {1,1,1,0}, so it is never empty
    jassert (! m_tempoGrid.empty());

    // First entry strictly after bar; the one before it is the tempo in effect at bar
    const auto it = std::upper_bound (m_tempoGrid.begin(), m_tempoGrid.end(), bar,
                                      [] (const Bar& position, const auto& entry) { return position < entry.first; });

    // A position before the first entry uses the song's initial tempo
    return it == m_tempoGrid.begin() ? it->second : std::prev (it)->second;
}
