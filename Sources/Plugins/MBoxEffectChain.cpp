#include "MBoxEffectChain.h"

namespace
{
    constexpr double maxTailSeconds = 10.0;
}

//==============================================================================
MBoxEffectChain::MBoxEffectChain (int numChannels)
    : m_numChannels (juce::jlimit (1, 2, numChannels))
{
}

MBoxEffectChain::~MBoxEffectChain()
{
    // A plugin deleted on another thread can deadlock: JUCE's AU instance holds its lock while
    // it waits for the message thread to dispose of the AU, and the message thread may be in
    // that AU's parameter callback, waiting for the same lock. So a chain dropped by the loader
    // (a cancelled or failed build) has its plugins deleted on the message thread, later.
    auto* messageManager = juce::MessageManager::getInstanceWithoutCreating();

    if (! m_plugins.empty() && messageManager != nullptr && ! messageManager->isThisTheMessageThread())
    {
        auto plugins = std::make_shared<std::vector<std::unique_ptr<Plugin>>> (std::move (m_plugins));

        if (juce::MessageManager::callAsync ([plugins]
                                             {
                                                 for (auto& plugin : *plugins)
                                                     plugin->m_instance->releaseResources();

                                                 plugins->clear();
                                             }))
            return;

        m_plugins = std::move (*plugins);  // no message loop any more: delete them here
    }

    for (auto& plugin : m_plugins)
        plugin->m_instance->releaseResources();
}

void MBoxEffectChain::addPlugin (std::unique_ptr<juce::AudioPluginInstance> instance, bool bypassed)
{
    auto plugin = std::make_unique<Plugin>();
    plugin->m_instance = std::move (instance);
    plugin->m_bypassed = bypassed;
    m_plugins.push_back (std::move (plugin));
}

juce::AudioPluginInstance* MBoxEffectChain::getPlugin (int index) const noexcept
{
    return juce::isPositiveAndBelow (index, (int) m_plugins.size()) ? m_plugins[(size_t) index]->m_instance.get() : nullptr;
}

void MBoxEffectChain::setBypassed (int index, bool bypassed) noexcept
{
    if (juce::isPositiveAndBelow (index, (int) m_plugins.size()))
        m_plugins[(size_t) index]->m_bypassed = bypassed;
}

//==============================================================================
// Layout choice: the chain's channel count when supported, then mono in / stereo out (common
// for amp simulators), then stereo. Only the main buses change; side-chains stay as they are.
//==============================================================================
void MBoxEffectChain::choosePluginLayout (Plugin& plugin)
{
    auto& instance = *plugin.m_instance;
    const auto mono = juce::AudioChannelSet::mono();
    const auto stereo = juce::AudioChannelSet::stereo();

    std::vector<std::pair<juce::AudioChannelSet, juce::AudioChannelSet>> candidates;

    if (m_numChannels == 1)
        candidates = { { mono, mono }, { mono, stereo }, { stereo, stereo } };
    else
        candidates = { { stereo, stereo }, { mono, stereo }, { mono, mono } };

    for (const auto& [input, output] : candidates)
    {
        auto layout = instance.getBusesLayout();

        if (layout.inputBuses.isEmpty() || layout.outputBuses.isEmpty())
            break;

        layout.inputBuses.getReference (0) = input;
        layout.outputBuses.getReference (0) = output;

        if (instance.checkBusesLayoutSupported (layout) && instance.setBusesLayout (layout))
            break;
    }

    plugin.m_numInputs = instance.getTotalNumInputChannels();
    plugin.m_numOutputs = instance.getTotalNumOutputChannels();
}

void MBoxEffectChain::prepare (double sampleRate, int maxBlockSize)
{
    m_maxBlockSize = maxBlockSize;
    int maxPluginChannels = m_numChannels;

    for (auto& plugin : m_plugins)
    {
        choosePluginLayout (*plugin);
        plugin->m_instance->setRateAndBufferSizeDetails (sampleRate, maxBlockSize);
        plugin->m_instance->prepareToPlay (sampleRate, maxBlockSize);
        maxPluginChannels = juce::jmax (maxPluginChannels, plugin->m_numInputs, plugin->m_numOutputs);
    }

    m_scratch.setSize (maxPluginChannels, maxBlockSize);
    m_midi.ensureSize (256);
}

int MBoxEffectChain::getLatencySamples() const noexcept
{
    int latency = 0;

    for (const auto& plugin : m_plugins)
        latency += plugin->m_instance->getLatencySamples();

    return latency;
}

double MBoxEffectChain::getTailLengthSeconds() const noexcept
{
    double tail = 0.0;

    for (const auto& plugin : m_plugins)
        tail = juce::jmax (tail, plugin->m_instance->getTailLengthSeconds());

    return juce::jmin (tail, maxTailSeconds);
}

void MBoxEffectChain::setCompensatedLatency (int totalLatency)
{
    m_delayLength = juce::jmax (0, totalLatency - getLatencySamples());
    m_delayPosition = 0;
    m_delay.setSize (m_numChannels, juce::jmax (1, m_delayLength));
    m_delay.clear();
}

void MBoxEffectChain::clearDelay() noexcept
{
    m_delay.clear();
    m_delayPosition = 0;
}

void MBoxEffectChain::warmUp (double seconds)
{
    if (m_plugins.empty() || m_maxBlockSize <= 0)
        return;

    juce::AudioBuffer<float> silence (m_numChannels, m_maxBlockSize);
    const auto sampleRate = m_plugins.front()->m_instance->getSampleRate();
    const auto numBlocks = (int) std::ceil (seconds * sampleRate / m_maxBlockSize);

    for (int i = 0; i < numBlocks; ++i)
    {
        silence.clear();
        process (silence.getArrayOfWritePointers(), m_maxBlockSize);
    }

    // Leave the compensation delay empty for the first real block
    m_delay.clear();
}

//==============================================================================
// Audio thread
//==============================================================================
void MBoxEffectChain::process (float* const* channels, int numFrames) noexcept
{
    jassert (numFrames <= m_maxBlockSize || m_plugins.empty());

    for (auto& plugin : m_plugins)
    {
        const auto numInputs = plugin->m_numInputs;
        const auto numOutputs = plugin->m_numOutputs;
        juce::AudioBuffer<float> buffer (m_scratch.getArrayOfWritePointers(), m_scratch.getNumChannels(), numFrames);
        buffer.clear();

        // Chain channels -> plugin inputs
        if (m_numChannels == 2 && numInputs == 1)
        {
            buffer.copyFrom (0, 0, channels[0], numFrames, 0.5f);
            buffer.addFrom (0, 0, channels[1], numFrames, 0.5f);
        }
        else
        {
            for (int ch = 0; ch < numInputs; ++ch)
                buffer.copyFrom (ch, 0, channels[juce::jmin (ch, m_numChannels - 1)], numFrames);
        }

        m_midi.clear();

        if (plugin->m_bypassed.load (std::memory_order_relaxed))
            plugin->m_instance->processBlockBypassed (buffer, m_midi);
        else
            plugin->m_instance->processBlock (buffer, m_midi);

        // Plugin outputs -> chain channels
        if (numOutputs <= 0)
            continue;

        if (m_numChannels == 1 && numOutputs >= 2)
        {
            juce::FloatVectorOperations::copyWithMultiply (channels[0], buffer.getReadPointer (0), 0.5f, numFrames);
            juce::FloatVectorOperations::addWithMultiply (channels[0], buffer.getReadPointer (1), 0.5f, numFrames);
        }
        else
        {
            for (int ch = 0; ch < m_numChannels; ++ch)
                juce::FloatVectorOperations::copy (channels[ch], buffer.getReadPointer (juce::jmin (ch, numOutputs - 1)), numFrames);
        }
    }

    if (m_delayLength == 0)
        return;

    auto position = m_delayPosition;

    for (int ch = 0; ch < m_numChannels; ++ch)
    {
        auto* ring = m_delay.getWritePointer (ch);
        auto* samples = channels[ch];
        position = m_delayPosition;

        for (int i = 0; i < numFrames; ++i)
        {
            const auto delayed = ring[position];
            ring[position] = samples[i];
            samples[i] = delayed;

            if (++position == m_delayLength)
                position = 0;
        }
    }

    m_delayPosition = position;
}
