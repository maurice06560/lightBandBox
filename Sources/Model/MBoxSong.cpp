#include "MBoxSong.h"
#include "../Audio/MBoxAudioClipCache.h"

#include <algorithm>
#include <cmath>
#include <map>

//==============================================================================
// Construction: a new song is 4/4 at defaultTempo, defaultTotalBar bars long
//==============================================================================
MBoxSong::MBoxSong (const juce::String& name)
    : m_name (name),
      m_tempoGrid { { Bar {}, defaultTempo } },
      m_signatureGrid { { Bar {}, Signature {} } }
{
}

MBoxSong::MBoxSong (const MBoxSong& other)
    : m_name (other.m_name),
      m_tempoGrid (other.m_tempoGrid),
      m_signatureGrid (other.m_signatureGrid),
      m_totalNbOfBars (other.m_totalNbOfBars),
      m_leftLocator (other.m_leftLocator),
      m_rightLocator (other.m_rightLocator),
      m_endSongAction (other.m_endSongAction),
      m_endStopDelay (other.m_endStopDelay),
      m_metronomeState (other.m_metronomeState),
      m_mixes (other.m_mixes)
{
    for (const auto& track : other.m_tracks)
        m_tracks.push_back (track->clone());
}

juce::String MBoxSong::getName() const  { return m_name; }

//==============================================================================
// Length
//==============================================================================
void MBoxSong::setTotalBar (const int bar)
{
    // The right locator must stay inside the song
    m_totalNbOfBars = juce::jmax (1, bar, m_rightLocator.m_bar);
}

int MBoxSong::getTotalBar() const  { return m_totalNbOfBars; }

void MBoxSong::setEndSongDelay (int seconds) noexcept
{
    m_endStopDelay = juce::jlimit (0, maxEndSongDelaySeconds, seconds);
}

//==============================================================================
// Tempo and signature grids
//==============================================================================
juce::Result MBoxSong::addTempo (const std::pair<Bar, int>& tempo)
{
    if (tempo.second < minTempo || tempo.second > maxTempo)
        return juce::Result::fail ("Tempo must be between " + juce::String (minTempo) + " and " + juce::String (maxTempo) + " BPM");

    if (! getTimeline().isValidPosition (tempo.first))
        return juce::Result::fail ("Invalid position " + juce::String (tempo.first.toString()));

    auto it = std::find_if (m_tempoGrid.begin(), m_tempoGrid.end(), [&] (const auto& entry) { return entry.first == tempo.first; });

    if (it != m_tempoGrid.end())
        it->second = tempo.second;
    else
        m_tempoGrid.push_back (tempo);

    sortAndCompleteGrids();
    extendRightLocatorToAudio();
    return juce::Result::ok();
}

juce::Result MBoxSong::removeTempo (const Bar& tempo)
{
    if (tempo == Bar {})
        return juce::Result::fail ("The tempo at bar 1 can't be removed");

    auto it = std::find_if (m_tempoGrid.begin(), m_tempoGrid.end(), [&] (const auto& entry) { return entry.first == tempo; });

    if (it == m_tempoGrid.end())
        return juce::Result::fail ("No tempo change at " + juce::String (tempo.toString()));

    m_tempoGrid.erase (it);
    extendRightLocatorToAudio();
    return juce::Result::ok();
}

juce::Result MBoxSong::addSignature (const std::pair<Bar, Signature>& signature)
{
    if (! signature.second.isValid())
        return juce::Result::fail ("Invalid time signature (denominators can be 2, 4, 8 or 16)");

    if (signature.first.m_bar < 1 || ! signature.first.isStartOfBar())
        return juce::Result::fail ("A time signature can only change at the start of a bar");

    auto it = std::find_if (m_signatureGrid.begin(), m_signatureGrid.end(),
                            [&] (const auto& entry) { return entry.first.m_bar == signature.first.m_bar; });

    if (it != m_signatureGrid.end())
        it->second = signature.second;
    else
        m_signatureGrid.push_back (signature);

    sortAndCompleteGrids();
    clampPositionsToSignatures();
    extendRightLocatorToAudio();
    return juce::Result::ok();
}

juce::Result MBoxSong::removeSignature (const Bar& signature)
{
    if (signature.m_bar == 1)
        return juce::Result::fail ("The time signature at bar 1 can't be removed");

    auto it = std::find_if (m_signatureGrid.begin(), m_signatureGrid.end(),
                            [&] (const auto& entry) { return entry.first.m_bar == signature.m_bar; });

    if (it == m_signatureGrid.end())
        return juce::Result::fail ("No time signature change at bar " + juce::String (signature.m_bar));

    m_signatureGrid.erase (it);
    clampPositionsToSignatures();
    extendRightLocatorToAudio();
    return juce::Result::ok();
}

MBoxTimeline MBoxSong::getTimeline() const
{
    return MBoxTimeline (m_tempoGrid, m_signatureGrid);
}

void MBoxSong::sortAndCompleteGrids()
{
    if (std::none_of (m_tempoGrid.begin(), m_tempoGrid.end(), [] (const auto& entry) { return entry.first == Bar {}; }))
        m_tempoGrid.push_back ({ Bar {}, m_tempoGrid.empty() ? defaultTempo : m_tempoGrid.front().second });

    if (std::none_of (m_signatureGrid.begin(), m_signatureGrid.end(), [] (const auto& entry) { return entry.first.m_bar == 1; }))
        m_signatureGrid.push_back ({ Bar {}, Signature {} });

    std::sort (m_tempoGrid.begin(), m_tempoGrid.end(), [] (const auto& a, const auto& b) { return a.first < b.first; });
    std::sort (m_signatureGrid.begin(), m_signatureGrid.end(), [] (const auto& a, const auto& b) { return a.first < b.first; });
}

//==============================================================================
// After a signature change, a position such as beat 4 of a bar that is now 3/4 no longer
// exists. Such positions are moved to the last valid beat and division of their bar.
//==============================================================================
Bar MBoxSong::clampPosition (const MBoxTimeline& timeline, Bar position) const
{
    const auto signature = timeline.getSignatureAtBar (position.m_bar);
    position.m_bar      = juce::jmax (1, position.m_bar);
    position.m_beat     = juce::jlimit (1, signature.m_numerator, position.m_beat);
    position.m_division = juce::jlimit (1, signature.divisionsPerBeatUnit(), position.m_division);
    position.m_tick     = juce::jlimit (0, ticksPerDivision - 1, position.m_tick);
    return position;
}

void MBoxSong::clampPositionsToSignatures()
{
    const auto timeline = getTimeline();

    for (auto& entry : m_tempoGrid)
        entry.first = clampPosition (timeline, entry.first);

    // Clamping can make two tempo entries land on the same position: keep the later one
    std::map<std::int64_t, std::pair<Bar, int>> uniqueTempos;

    for (const auto& entry : m_tempoGrid)
        uniqueTempos[timeline.barToTicks (entry.first)] = entry;

    m_tempoGrid.clear();

    for (const auto& [ticks, entry] : uniqueTempos)
        m_tempoGrid.push_back (entry);

    m_leftLocator = clampPosition (timeline, m_leftLocator);
    m_rightLocator = clampPosition (timeline, m_rightLocator);

    for (auto& track : m_tracks)
    {
        if (auto* audio = dynamic_cast<MBoxAudioTrack*> (track.get()))
            audio->setAudioStart (clampPosition (timeline, audio->getAudioStart()));
        else if (auto* physical = dynamic_cast<MBoxPhysicalTrack*> (track.get()))
            for (auto& change : physical->m_bankSlots)
                change.first = clampPosition (timeline, change.first);
        else if (auto* info = dynamic_cast<MBoxInfoTrack*> (track.get()))
            for (auto& click : info->m_infoClicks)
                click = clampPosition (timeline, click);
    }
}

//==============================================================================
// Locators
//==============================================================================
juce::Result MBoxSong::setLeftLocator (const Bar& position)
{
    if (! getTimeline().isValidPosition (position))
        return juce::Result::fail ("Invalid position " + juce::String (position.toString()));

    if (position >= m_rightLocator)
        return juce::Result::fail ("The left locator must be before the right locator");

    m_leftLocator = position;
    return juce::Result::ok();
}

juce::Result MBoxSong::setRightLocator (const Bar& position)
{
    if (! getTimeline().isValidPosition (position))
        return juce::Result::fail ("Invalid position " + juce::String (position.toString()));

    if (position <= m_leftLocator)
        return juce::Result::fail ("The right locator must be after the left locator");

    m_rightLocator = position;
    m_totalNbOfBars = juce::jmax (m_totalNbOfBars, position.m_bar);
    return juce::Result::ok();
}

void MBoxSong::extendRightLocatorToAudio()
{
    const auto timeline = getTimeline();
    std::int64_t furthestEndTicks = timeline.barToTicks (m_rightLocator);

    for (const auto& track : m_tracks)
    {
        const auto* audio = dynamic_cast<const MBoxAudioTrack*> (track.get());

        if (audio == nullptr || audio->getLengthInFrames() <= 0)
            continue;

        // The end of the WAV, rounded up to the next tick so the whole file is inside the song
        const auto endFrame = timeline.barToFrames (audio->getAudioStart()) + audio->getLengthInFrames();
        const auto endTicks = (std::int64_t) std::ceil (timeline.framesToTicks (endFrame) - 1.0e-6);
        furthestEndTicks = juce::jmax (furthestEndTicks, endTicks);
    }

    const auto furthestEnd = timeline.ticksToBar (furthestEndTicks);

    if (furthestEnd > m_rightLocator)
    {
        m_rightLocator = furthestEnd;
        m_totalNbOfBars = juce::jmax (m_totalNbOfBars, furthestEnd.m_bar);
    }
}

//==============================================================================
// Tracks
//==============================================================================
juce::Result MBoxSong::addTrack (std::unique_ptr<MBoxTrack> track, int* newId)
{
    if (track == nullptr)
        return juce::Result::fail ("No track given");

    if ((int) m_tracks.size() >= maxNumberOfTrackPerSong)
        return juce::Result::fail ("A song can have at most " + juce::String (maxNumberOfTrackPerSong) + " tracks");

    int nextId = 1;

    for (const auto& existing : m_tracks)
        nextId = juce::jmax (nextId, existing->getId() + 1);

    track->m_id = nextId;

    if (newId != nullptr)
        *newId = nextId;

    m_tracks.push_back (std::move (track));
    ensureMixesMatchTracks();
    extendRightLocatorToAudio();
    return juce::Result::ok();
}

juce::Result MBoxSong::removeTrack (int trackId)
{
    auto it = std::find_if (m_tracks.begin(), m_tracks.end(), [&] (const auto& track) { return track->getId() == trackId; });

    if (it == m_tracks.end())
        return juce::Result::fail ("No track " + juce::String (trackId));

    m_tracks.erase (it);
    ensureMixesMatchTracks();
    return juce::Result::ok();
}

MBoxTrack* MBoxSong::getTrack (int trackId) const
{
    for (const auto& track : m_tracks)
        if (track->getId() == trackId)
            return track.get();

    return nullptr;
}

juce::Result MBoxSong::setAudioFile (int trackId, const juce::File& file)
{
    auto* audio = dynamic_cast<MBoxAudioTrack*> (getTrack (trackId));

    if (audio == nullptr)
        return juce::Result::fail ("No audio track " + juce::String (trackId));

    std::int64_t lengthInFrames = 0;
    const auto result = readSupportedWavLength (file, lengthInFrames);

    if (result.failed())
        return result;

    audio->setFilename (file, lengthInFrames);
    extendRightLocatorToAudio();
    return juce::Result::ok();
}

juce::Result MBoxSong::setAudioStart (int trackId, const Bar& position)
{
    auto* audio = dynamic_cast<MBoxAudioTrack*> (getTrack (trackId));

    if (audio == nullptr)
        return juce::Result::fail ("No audio track " + juce::String (trackId));

    if (! getTimeline().isValidPosition (position))
        return juce::Result::fail ("Invalid position " + juce::String (position.toString()));

    audio->setAudioStart (position);
    extendRightLocatorToAudio();
    return juce::Result::ok();
}

//==============================================================================
// Mixes
//==============================================================================
void MBoxSong::ensureMixesMatchTracks()
{
    for (auto& mix : m_mixes)
    {
        std::vector<MBoxTrackMix> matched;

        for (const auto& track : m_tracks)
        {
            auto it = std::find_if (mix.begin(), mix.end(), [&] (const auto& trackMix) { return trackMix.m_trackId == track->getId(); });

            if (it != mix.end())
                matched.push_back (std::move (*it));
            else
            {
                MBoxTrackMix trackMix;
                trackMix.m_trackId = track->getId();
                matched.push_back (std::move (trackMix));
            }
        }

        mix = std::move (matched);
    }
}

MBoxTrackMix* MBoxSong::getTrackMix (int bus, int trackId)
{
    if (bus < 1 || bus > numberOfMixBus)
        return nullptr;

    for (auto& trackMix : m_mixes[(size_t) bus - 1])
        if (trackMix.m_trackId == trackId)
            return &trackMix;

    return nullptr;
}

const MBoxTrackMix* MBoxSong::getTrackMix (int bus, int trackId) const
{
    return const_cast<MBoxSong*> (this)->getTrackMix (bus, trackId);
}

const std::vector<MBoxTrackMix>& MBoxSong::getMix (int bus) const
{
    jassert (bus >= 1 && bus <= numberOfMixBus);
    return m_mixes[(size_t) juce::jlimit (1, numberOfMixBus, bus) - 1];
}

std::vector<MBoxTrackMix>& MBoxSong::getMix (int bus)
{
    jassert (bus >= 1 && bus <= numberOfMixBus);
    return m_mixes[(size_t) juce::jlimit (1, numberOfMixBus, bus) - 1];
}

//==============================================================================
// Loading from storage
//==============================================================================
void MBoxSong::setGridsForStorage (std::vector<std::pair<Bar, int>> tempoGrid, std::vector<std::pair<Bar, Signature>> signatureGrid)
{
    m_tempoGrid = std::move (tempoGrid);
    m_signatureGrid = std::move (signatureGrid);
}

void MBoxSong::addTrackForStorage (std::unique_ptr<MBoxTrack> track)
{
    m_tracks.push_back (std::move (track));
}

juce::Result MBoxSong::validateAfterLoad()
{
    for (const auto& [position, bpm] : m_tempoGrid)
        if (bpm < minTempo || bpm > maxTempo)
            return juce::Result::fail ("Song " + m_name + ": invalid tempo " + juce::String (bpm) + " at " + juce::String (position.toString()));

    for (const auto& [position, signature] : m_signatureGrid)
        if (! signature.isValid() || ! position.isStartOfBar())
            return juce::Result::fail ("Song " + m_name + ": invalid time signature at " + juce::String (position.toString()));

    if ((int) m_tracks.size() > maxNumberOfTrackPerSong)
        return juce::Result::fail ("Song " + m_name + ": more than " + juce::String (maxNumberOfTrackPerSong) + " tracks");

    for (size_t i = 0; i < m_tracks.size(); ++i)
        for (size_t j = i + 1; j < m_tracks.size(); ++j)
            if (m_tracks[i]->getId() == m_tracks[j]->getId())
                return juce::Result::fail ("Song " + m_name + ": two tracks have the id " + juce::String (m_tracks[i]->getId()));

    sortAndCompleteGrids();
    clampPositionsToSignatures();

    if (m_leftLocator >= m_rightLocator)
        return juce::Result::fail ("Song " + m_name + ": the left locator must be before the right locator");

    m_totalNbOfBars = juce::jmax (m_totalNbOfBars, m_rightLocator.m_bar);
    ensureMixesMatchTracks();
    return juce::Result::ok();
}
