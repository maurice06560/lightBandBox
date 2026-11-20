#pragma once

#include "MBoxTypes.h"

#include <juce_audio_processors/juce_audio_processors.h>

#include <array>
#include <vector>

//==============================================================================
// One plugin in an effect chain (the diagram's IAudioEffectInstance), as stored in the data
// files. It holds what's needed to find and restore the plugin on any computer: its identity
// (never its file path, which differs between machines) and its saved state.
// The engine creates the actual plugin instances from it when a playlist loads.
//==============================================================================
struct MBoxEffect
{
    juce::String m_name;
    juce::String m_format;        // "VST3" or "AudioUnit"
    juce::String m_manufacturer;
    juce::String m_identifier;    // PluginDescription::fileOrIdentifier, used only as a lookup hint
    int m_uniqueId { 0 };
    juce::String m_version;
    juce::MemoryBlock m_state;    // AudioProcessor::getStateInformation()
    bool m_bypassed { false };

    // Takes the identity from a scanned plugin description, with an empty state
    static MBoxEffect fromDescription (const juce::PluginDescription& description);

    // A description to search the scanned plugin list with
    juce::PluginDescription toDescription() const;

    bool operator== (const MBoxEffect& other) const;
};

//==============================================================================
// A named chain of effects that a physical input can switch to. Chains are mono in, mono out.
//==============================================================================
struct MBoxBankSlot
{
    juce::String m_name;
    std::vector<MBoxEffect> m_effectChain;
};

//==============================================================================
// A bank of numberOfSlotsPerBanks slots. Banks are global to the rack, not per song.
//==============================================================================
struct MBoxBank
{
    juce::String m_name;
    std::array<MBoxBankSlot, numberOfSlotsPerBanks> m_bankSlots;
};

using MBoxBanks = std::array<MBoxBank, numberOfBanks>;

//==============================================================================
// Refers to one slot of one bank. Both numbers are 1-based.
//==============================================================================
struct MBoxSlotRef
{
    int m_bank { 1 };
    int m_slot { 1 };

    bool isValid() const noexcept
    {
        return m_bank >= 1 && m_bank <= numberOfBanks && m_slot >= 1 && m_slot <= numberOfSlotsPerBanks;
    }

    bool operator== (const MBoxSlotRef& other) const noexcept  { return m_bank == other.m_bank && m_slot == other.m_slot; }
    bool operator!= (const MBoxSlotRef& other) const noexcept  { return ! (*this == other); }
    bool operator<  (const MBoxSlotRef& other) const noexcept
    {
        return m_bank != other.m_bank ? m_bank < other.m_bank : m_slot < other.m_slot;
    }
};

// Returns the slot a reference points to. The reference must be valid.
MBoxBankSlot& getBankSlot (MBoxBanks& banks, const MBoxSlotRef& slot);
const MBoxBankSlot& getBankSlot (const MBoxBanks& banks, const MBoxSlotRef& slot);

// Banks with default names ("Bank 1", "Slot 1"...) and empty chains
MBoxBanks createDefaultBanks();
