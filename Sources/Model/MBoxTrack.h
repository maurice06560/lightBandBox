#pragma once

#include "MBoxBank.h"
#include "MBoxTypes.h"

#include <juce_core/juce_core.h>

#include <memory>
#include <optional>
#include <utility>
#include <vector>

enum class TrackType
{
    Audio,     // a WAV file
    Physical,  // a live mono input of the audio interface
    Info       // synthesised informational cue clicks
};

const char* toString (TrackType type);
bool fromString (const std::string& text, TrackType& result);

//==============================================================================
// Base class of the tracks of a song. A track's id is unique within its song and is how
// mixes and the protocol refer to it. Tracks are plain data: the engine builds its own
// runtime objects from them when a playlist loads.
//==============================================================================
class MBoxTrack
{
public:
    MBoxTrack (int id, const juce::String& name);
    virtual ~MBoxTrack() = default;

    virtual TrackType getType() const = 0;

    // A deep copy, used to hand a snapshot of a song to the loader threads
    virtual std::unique_ptr<MBoxTrack> clone() const = 0;

    int getId() const noexcept  { return m_id; }
    void setName (const juce::String& name);
    juce::String getName() const;

protected:
    MBoxTrack (const MBoxTrack&) = default;

private:
    friend class MBoxSong;  // assigns the id when the track is added

    int m_id { 1 };
    juce::String m_name;
};

//==============================================================================
// A track playing a 44100 Hz, 16-bit stereo WAV file, starting at m_audioStart.
//==============================================================================
class MBoxAudioTrack final : public MBoxTrack
{
public:
    MBoxAudioTrack (int id, const juce::String& name);

    TrackType getType() const override  { return TrackType::Audio; }
    std::unique_ptr<MBoxTrack> clone() const override;

    void setAudioStart (const Bar& bar);
    Bar getAudioStart() const;

    // Sets the file and its length in frames. Use MBoxSong::setAudioFile, which reads the
    // length from the file and moves the right locator when the audio ends after it.
    void setFilename (const juce::File& filename, std::int64_t lengthInFrames);
    juce::File getFilename() const;
    std::int64_t getLengthInFrames() const noexcept  { return m_lengthInFrames; }

private:
    Bar m_audioStart { 1, 1, 1, 0 };
    juce::File m_filename;
    std::int64_t m_lengthInFrames { 0 };
};

//==============================================================================
// A live mono input. Its signal goes through the effect chain of a bank slot, then to the
// optional DI output and to the mixes. m_bankSlots is the slot automation: each entry
// switches the chain at a position. Before the first entry, the first entry's slot is used.
// Channel numbers are 0-based; an output of -1 means none.
//==============================================================================
class MBoxPhysicalTrack final : public MBoxTrack
{
public:
    MBoxPhysicalTrack (int id, const juce::String& name);

    TrackType getType() const override  { return TrackType::Physical; }
    std::unique_ptr<MBoxTrack> clone() const override;

    void setPhysicalInput (const int physicalInput);
    int getPhysicalInput() const;
    void setDIOutput (const int DIOutput);
    int getDIOutput() const;

    // Adds a slot change, replacing any change already at that position. Keeps the list sorted.
    void setBankSlotChange (const Bar& position, const MBoxSlotRef& slot);
    bool removeBankSlotChange (const Bar& position);
    const std::vector<std::pair<Bar, MBoxSlotRef>>& getBankSlotChanges() const noexcept  { return m_bankSlots; }

    // The slot active at a position, if the track has any slot change
    std::optional<MBoxSlotRef> getSlotAt (const Bar& position) const;

private:
    friend class MBoxSong;  // clamps positions after a signature change

    int m_physicalInput { 0 };
    int m_DIOutput { -1 };
    std::vector<std::pair<Bar, MBoxSlotRef>> m_bankSlots;
};

//==============================================================================
// Informational cue clicks. Each entry starts a group of clicks, one per beat-unit, as many
// as the numerator of the signature at that position, with the first one accented.
//==============================================================================
class MBoxInfoTrack final : public MBoxTrack
{
public:
    MBoxInfoTrack (int id, const juce::String& name);

    TrackType getType() const override  { return TrackType::Info; }
    std::unique_ptr<MBoxTrack> clone() const override;

    // Adds a group start (ignored when already present). Keeps the list sorted.
    void addInfoClick (const Bar& position);
    bool removeInfoClick (const Bar& position);
    const std::vector<Bar>& getInfoClicks() const noexcept  { return m_infoClicks; }

private:
    friend class MBoxSong;

    std::vector<Bar> m_infoClicks;
};
