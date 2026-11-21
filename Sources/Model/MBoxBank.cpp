#include "MBoxBank.h"

//==============================================================================
// MBoxEffect
//==============================================================================
MBoxEffect MBoxEffect::fromDescription (const juce::PluginDescription& description)
{
    MBoxEffect effect;
    effect.m_name         = description.name;
    effect.m_format       = description.pluginFormatName;
    effect.m_manufacturer = description.manufacturerName;
    effect.m_identifier   = description.fileOrIdentifier;
    effect.m_uniqueId     = description.uniqueId;
    effect.m_version      = description.version;
    return effect;
}

juce::PluginDescription MBoxEffect::toDescription() const
{
    juce::PluginDescription description;
    description.name             = m_name;
    description.pluginFormatName = m_format;
    description.manufacturerName = m_manufacturer;
    description.fileOrIdentifier = m_identifier;
    description.uniqueId         = m_uniqueId;
    description.version          = m_version;
    return description;
}

bool MBoxEffect::operator== (const MBoxEffect& other) const
{
    return m_name == other.m_name && m_format == other.m_format && m_manufacturer == other.m_manufacturer
        && m_identifier == other.m_identifier && m_uniqueId == other.m_uniqueId && m_version == other.m_version
        && m_state == other.m_state && m_bypassed == other.m_bypassed;
}

//==============================================================================
// Banks
//==============================================================================
MBoxBankSlot& getBankSlot (MBoxBanks& banks, const MBoxSlotRef& slot)
{
    jassert (slot.isValid());
    return banks[(size_t) slot.m_bank - 1].m_bankSlots[(size_t) slot.m_slot - 1];
}

const MBoxBankSlot& getBankSlot (const MBoxBanks& banks, const MBoxSlotRef& slot)
{
    jassert (slot.isValid());
    return banks[(size_t) slot.m_bank - 1].m_bankSlots[(size_t) slot.m_slot - 1];
}

MBoxBanks createDefaultBanks()
{
    MBoxBanks banks;

    for (size_t bank = 0; bank < banks.size(); ++bank)
    {
        banks[bank].m_name = "Bank " + juce::String ((int) bank + 1);

        for (size_t slot = 0; slot < banks[bank].m_bankSlots.size(); ++slot)
            banks[bank].m_bankSlots[slot].m_name = "Slot " + juce::String ((int) slot + 1);
    }

    return banks;
}
