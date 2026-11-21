#include "MBoxLiveInput.h"
#include "../Model/MBoxTypes.h"

namespace
{
    constexpr double minTailSeconds = 1.0;
}

//==============================================================================
MBoxLiveInput::MBoxLiveInput()
{
    m_entries.push_back ({ std::nullopt, std::make_unique<MBoxEffectChain> (1) });
}

void MBoxLiveInput::addSlotChain (const MBoxSlotRef& slot, std::unique_ptr<MBoxEffectChain> chain)
{
    jassert (chain != nullptr && chain->getNumChannels() == 1);

    if (findEntry (slot) == 0)
        m_entries.push_back ({ slot, std::move (chain) });
}

MBoxEffectChain* MBoxLiveInput::getSlotChain (const MBoxSlotRef& slot) const
{
    for (const auto& entry : m_entries)
        if (entry.m_slot == slot)
            return entry.m_chain.get();

    return nullptr;
}

std::vector<MBoxEffectChain*> MBoxLiveInput::getAllChains() const
{
    std::vector<MBoxEffectChain*> chains;

    for (const auto& entry : m_entries)
        chains.push_back (entry.m_chain.get());

    return chains;
}

int MBoxLiveInput::getLatencySamples() const noexcept
{
    int latency = 0;

    for (const auto& entry : m_entries)
        latency = juce::jmax (latency, entry.m_chain->getLatencySamples());

    return latency;
}

void MBoxLiveInput::prepare (double sampleRate, int maxBlockSize, int totalLatency)
{
    m_sampleRate = sampleRate;
    m_fadeFrames = juce::jmax (1, (int) (transportFadeSeconds * sampleRate));
    m_input.allocate ((size_t) maxBlockSize, true);
    m_output.allocate ((size_t) maxBlockSize, true);
    m_active = -1;

    for (auto& entry : m_entries)
    {
        entry.m_chain->setCompensatedLatency (totalLatency);
        entry.m_inputGain = entry.m_targetInputGain = 0.0f;
        entry.m_tailRemaining = 0;
    }
}

void MBoxLiveInput::warmUp (double seconds)
{
    for (auto& entry : m_entries)
        entry.m_chain->warmUp (seconds);
}

int MBoxLiveInput::findEntry (const std::optional<MBoxSlotRef>& slot) const noexcept
{
    if (slot.has_value())
        for (size_t i = 1; i < m_entries.size(); ++i)
            if (m_entries[i].m_slot == slot)
                return (int) i;

    return 0;
}

//==============================================================================
// Audio thread
//==============================================================================
void MBoxLiveInput::process (float* samples, int numFrames, const std::optional<MBoxSlotRef>& slot) noexcept
{
    const auto wanted = findEntry (slot);

    if (wanted != m_active)
    {
        if (m_active < 0)
        {
            m_entries[(size_t) wanted].m_inputGain = 1.0f;  // first use: no fade
        }
        else
        {
            auto& previous = m_entries[(size_t) m_active];
            previous.m_targetInputGain = 0.0f;
            const auto tail = juce::jmax (minTailSeconds, previous.m_chain->getTailLengthSeconds());
            previous.m_tailRemaining = m_fadeFrames + (int) (tail * m_sampleRate);
        }

        m_entries[(size_t) wanted].m_targetInputGain = 1.0f;
        m_entries[(size_t) wanted].m_tailRemaining = 0;
        m_active = wanted;
    }

    juce::FloatVectorOperations::clear (m_output.get(), numFrames);
    const auto step = 1.0f / (float) m_fadeFrames;

    for (auto& entry : m_entries)
    {
        const bool isActive = &entry == &m_entries[(size_t) m_active];

        if (! isActive && entry.m_tailRemaining <= 0)
            continue;

        // Input with its fade ramp
        auto gain = entry.m_inputGain;

        for (int i = 0; i < numFrames; ++i)
        {
            gain = entry.m_targetInputGain > gain ? juce::jmin (entry.m_targetInputGain, gain + step)
                                                  : juce::jmax (entry.m_targetInputGain, gain - step);
            m_input[i] = samples[i] * gain;
        }

        entry.m_inputGain = gain;

        float* channels[] = { m_input.get() };
        entry.m_chain->process (channels, numFrames);
        juce::FloatVectorOperations::add (m_output.get(), m_input.get(), numFrames);

        if (! isActive)
            entry.m_tailRemaining -= numFrames;
    }

    juce::FloatVectorOperations::copy (samples, m_output.get(), numFrames);
}
