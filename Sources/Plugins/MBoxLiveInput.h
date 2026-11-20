#pragma once

#include "MBoxEffectChain.h"
#include "../Model/MBoxBank.h"

#include <optional>

//==============================================================================
// Processes one live mono input through the effect chain of its current bank slot.
//
// It holds one prepared chain per slot the playlist uses on this input, plus a "dry" chain
// (no plugins) for when no slot is selected. All chains are padded to the same latency.
//
// Switching slots (process() called with a different slot):
// - The new chain's input fades in and the old chain's input fades out over 10 ms.
// - The old chain's output is not faded: it keeps running on silence until its tail has
//   rung out (the plugin's reported tail, at least 1 s, at most 10 s), so a reverb or delay
//   from the previous slot decays naturally instead of being cut.
// The first slot used after prepare() starts at full level, without a fade.
//==============================================================================
class MBoxLiveInput
{
public:
    MBoxLiveInput();

    // Adds the chain for a slot. Call before prepare().
    void addSlotChain (const MBoxSlotRef& slot, std::unique_ptr<MBoxEffectChain> chain);
    MBoxEffectChain* getSlotChain (const MBoxSlotRef& slot) const;
    std::vector<MBoxEffectChain*> getAllChains() const;

    // The largest latency among the slot chains
    int getLatencySamples() const noexcept;

    // Pads every chain to totalLatency and resets the switching state. The chains must already
    // be prepared (their latency is only known after MBoxEffectChain::prepare). Off the audio thread.
    void prepare (double sampleRate, int maxBlockSize, int totalLatency);
    void warmUp (double seconds);

    // Audio thread. Processes the mono signal in place with the chain of the given slot
    // (the dry chain when the slot is empty or has no chain here).
    void process (float* samples, int numFrames, const std::optional<MBoxSlotRef>& slot) noexcept;

private:
    struct Entry
    {
        std::optional<MBoxSlotRef> m_slot;  // empty for the dry chain
        std::unique_ptr<MBoxEffectChain> m_chain;
        float m_inputGain { 0.0f };
        float m_targetInputGain { 0.0f };
        int m_tailRemaining { 0 };  // frames left to process after the input has faded out
    };

    int findEntry (const std::optional<MBoxSlotRef>& slot) const noexcept;

    std::vector<Entry> m_entries;  // m_entries[0] is the dry chain
    int m_active { -1 };
    double m_sampleRate { 44100.0 };
    int m_fadeFrames { 441 };
    juce::HeapBlock<float> m_input, m_output;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MBoxLiveInput)
};
