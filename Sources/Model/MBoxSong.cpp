#include "MBoxSong.h"
#include "MBoxAudioTrack.h"

#include <algorithm>

void MBoxSong::addAudioTrack (const Bar& startAudio, const juce::String& fileName)
{
    m_tracks.push_back (std::make_unique<MBoxAudioTrack> (startAudio, fileName));
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
