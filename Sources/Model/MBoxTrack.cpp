#include "MBoxTrack.h"

#include <algorithm>

//==============================================================================
// TrackType names
//==============================================================================
const char* toString (TrackType type)
{
    switch (type)
    {
        case TrackType::Physical: return "physical";
        case TrackType::Info:     return "info";
        case TrackType::Audio:    break;
    }

    return "audio";
}

bool fromString (const std::string& text, TrackType& result)
{
    if (text == "audio")    { result = TrackType::Audio;    return true; }
    if (text == "physical") { result = TrackType::Physical; return true; }
    if (text == "info")     { result = TrackType::Info;     return true; }
    return false;
}

//==============================================================================
// MBoxTrack
//==============================================================================
MBoxTrack::MBoxTrack (int id, const juce::String& name)
    : m_id (id), m_name (name)
{
}

void MBoxTrack::setName (const juce::String& name)  { m_name = name; }
juce::String MBoxTrack::getName() const             { return m_name; }

//==============================================================================
// MBoxAudioTrack
//==============================================================================
MBoxAudioTrack::MBoxAudioTrack (int id, const juce::String& name)
    : MBoxTrack (id, name)
{
}

std::unique_ptr<MBoxTrack> MBoxAudioTrack::clone() const  { return std::make_unique<MBoxAudioTrack> (*this); }

void MBoxAudioTrack::setAudioStart (const Bar& bar)  { m_audioStart = bar; }
Bar MBoxAudioTrack::getAudioStart() const            { return m_audioStart; }

void MBoxAudioTrack::setFilename (const juce::File& filename, std::int64_t lengthInFrames)
{
    m_filename = filename;
    m_lengthInFrames = lengthInFrames;
}

juce::File MBoxAudioTrack::getFilename() const  { return m_filename; }

//==============================================================================
// MBoxPhysicalTrack
//==============================================================================
MBoxPhysicalTrack::MBoxPhysicalTrack (int id, const juce::String& name)
    : MBoxTrack (id, name)
{
}

std::unique_ptr<MBoxTrack> MBoxPhysicalTrack::clone() const  { return std::make_unique<MBoxPhysicalTrack> (*this); }

void MBoxPhysicalTrack::setPhysicalInput (const int physicalInput)  { m_physicalInput = juce::jmax (0, physicalInput); }
int MBoxPhysicalTrack::getPhysicalInput() const                     { return m_physicalInput; }
void MBoxPhysicalTrack::setDIOutput (const int DIOutput)            { m_DIOutput = juce::jmax (-1, DIOutput); }
int MBoxPhysicalTrack::getDIOutput() const                          { return m_DIOutput; }

void MBoxPhysicalTrack::setBankSlotChange (const Bar& position, const MBoxSlotRef& slot)
{
    auto it = std::lower_bound (m_bankSlots.begin(), m_bankSlots.end(), position,
                                [] (const auto& entry, const Bar& value) { return entry.first < value; });

    if (it != m_bankSlots.end() && it->first == position)
        it->second = slot;
    else
        m_bankSlots.insert (it, { position, slot });
}

bool MBoxPhysicalTrack::removeBankSlotChange (const Bar& position)
{
    const auto sizeBefore = m_bankSlots.size();
    m_bankSlots.erase (std::remove_if (m_bankSlots.begin(), m_bankSlots.end(),
                                       [&] (const auto& entry) { return entry.first == position; }),
                       m_bankSlots.end());
    return m_bankSlots.size() != sizeBefore;
}

std::optional<MBoxSlotRef> MBoxPhysicalTrack::getSlotAt (const Bar& position) const
{
    if (m_bankSlots.empty())
        return std::nullopt;

    // The last change at or before the position, or the first change when none is
    auto it = std::upper_bound (m_bankSlots.begin(), m_bankSlots.end(), position,
                                [] (const Bar& value, const auto& entry) { return value < entry.first; });
    return it == m_bankSlots.begin() ? it->second : std::prev (it)->second;
}

//==============================================================================
// MBoxInfoTrack
//==============================================================================
MBoxInfoTrack::MBoxInfoTrack (int id, const juce::String& name)
    : MBoxTrack (id, name)
{
}

std::unique_ptr<MBoxTrack> MBoxInfoTrack::clone() const  { return std::make_unique<MBoxInfoTrack> (*this); }

void MBoxInfoTrack::addInfoClick (const Bar& position)
{
    auto it = std::lower_bound (m_infoClicks.begin(), m_infoClicks.end(), position);

    if (it == m_infoClicks.end() || *it != position)
        m_infoClicks.insert (it, position);
}

bool MBoxInfoTrack::removeInfoClick (const Bar& position)
{
    auto it = std::find (m_infoClicks.begin(), m_infoClicks.end(), position);

    if (it == m_infoClicks.end())
        return false;

    m_infoClicks.erase (it);
    return true;
}
