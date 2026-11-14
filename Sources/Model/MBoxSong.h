#pragma once

#include "MBoxTrack.h"
#include "MBoxCommonStructures.h"

#include <juce_core/juce_core.h>

#include <cstdint>
#include <memory>
#include <vector>

using tempoGrid = std::vector<std::pair<Bar, int>>;
using signatureGrid = std::vector<std::pair<Bar, Signature>>;

class MBoxSong final
{
public:
    MBoxSong() = default;
    explicit MBoxSong (const juce::String& name) : m_name (name) {}
    ~MBoxSong() = default;

    const juce::String& getName() const { return m_name; }
    /** Returns the song's tracks. The song keeps ownership, so the pointers stay valid only while the song and its tracks exist. */
    std::vector<MBoxTrack*> getTracks() const;
    /** Returns the tempo changes, sorted by position. */
    const tempoGrid& getTempoGrid() const { return m_tempoGrid; }
    /** Returns the time signature changes, sorted by position. */
    const signatureGrid& getSignatureGrid() const { return m_signatureGrid; }
    /** Returns the position where the song starts. */
    const Bar& getSongStartPosition() const { return m_songStartPosition; }
    /** Returns the position where the song ends. */
    const Bar& getSongEndPosition() const { return m_songEndPosition; }

    void setSongStartPosition (const Bar& position) { m_songStartPosition = position; }
    void setSongEndPosition (const Bar& position) { m_songEndPosition = position; }

    /** Adds an audio track that plays fileName starting at startAudio. */
    void addAudioTrack (const Bar& startAudio, const juce::String& fileName);

    /** Sets the tempo (in BPM) from the given position. Replaces any tempo already at that position. */
    void addTempo(const Bar& bar, int tempo);
    /** Sets the time signature from the given position. Replaces any signature already at that position. */
    void addSignature(const Bar& bar, const Signature& signature);

    /** Returns the tempo (in BPM) in effect at the given position: the last tempo set at or before it. */
    int getTempoForBar(const Bar& bar) const;
    //Signature getSignatureForBar(const Bar bar);
private:
    juce::String m_name;
    std::vector<std::unique_ptr<MBoxTrack>> m_tracks;

    tempoGrid m_tempoGrid { { {1,1,1,0}, 120 } };
    signatureGrid m_signatureGrid { { {1,1,1,0}, {4, 4} } };

    Bar m_songStartPosition{1,1,1,0};
    Bar m_songEndPosition{1,1,1,0};
};
