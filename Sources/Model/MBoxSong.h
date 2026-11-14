#pragma once

#include "MBoxTrack.h"
#include "MBoxPhysicalTrack.h"
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

    /** Adds an audio track that plays audioFile starting at startAudio. The track keeps the file name only.
        If the file ends after the song end position, the song end position moves to the end of the file. */
    void addAudioTrack (const Bar& startAudio, const juce::File& audioFile);

    /** Adds a track fed by the given physical input. */
    void addPhysicalTrack (int physicalInput);

    /** Sets the tempo (in BPM) from the given position. Replaces any tempo already at that position. */
    void addTempo(const Bar& bar, int tempo);
    /** Sets the time signature from the given position. Replaces any signature already at that position. */
    void addSignature(const Bar& bar, const Signature& signature);

    /** Returns the tempo (in BPM) in effect at the given position: the last tempo set at or before it. */
    int getTempoForBar(const Bar& bar) const;
    //Signature getSignatureForBar(const Bar bar);

    // =============================================================================
    // Position conversions
    // =============================================================================
    /** Converts a position to an absolute tick count from the start of the song ({1,1,1,0} is tick 0).
        A beat is always Bar::ticksPerBeat (960) ticks, and each bar holds as many beats as the numerator
        of the signature in effect. Signature changes apply from the start of the bar they are set in. */
    std::int64_t convertBarToTick (const Bar& bar) const;

    /** Converts an absolute tick count to a position. The inverse of convertBarToTick. Negative ticks give {1,1,1,0}. */
    Bar convertTickToBar (std::int64_t ticks) const;

    /** Converts an absolute tick count to seconds from the start of the song, following the tempo grid. */
    double convertTickToSeconds (double ticks) const;

    /** Converts seconds from the start of the song to an absolute (fractional) tick count. The inverse of convertTickToSeconds. */
    double convertSecondsToTick (double seconds) const;
private:
    juce::String m_name;
    std::vector<std::unique_ptr<MBoxTrack>> m_tracks;

    tempoGrid m_tempoGrid { { {1,1,1,0}, 120 } };
    signatureGrid m_signatureGrid { { {1,1,1,0}, {4, 4} } };

    Bar m_songStartPosition{1,1,1,0};
    Bar m_songEndPosition{1,1,1,0};
};
