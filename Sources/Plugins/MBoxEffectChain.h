#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include <atomic>
#include <memory>
#include <vector>

//==============================================================================
// A runtime chain of hosted plugins processing 1 (mono) or 2 (stereo) channels in place.
//
// - Each plugin is given the chain's channel count when it supports it. Otherwise a mono
//   signal is duplicated into a stereo plugin and its output averaged back to mono (and a
//   stereo signal is averaged into a mono plugin and copied to both sides).
// - The chain can be padded with a delay (setCompensatedLatency) so that every chain in a
//   playlist has the same total latency, which keeps live inputs, WAVs and clicks aligned.
// - A bypassed plugin calls processBlockBypassed, which keeps its latency, so bypassing
//   never shifts timing.
//
// Life cycle: create and add plugins on the message thread, then prepare(),
// setCompensatedLatency() and warmUp() off the audio thread, then process() on the audio
// thread only. An empty chain is valid (it only applies the compensation delay).
//==============================================================================
class MBoxEffectChain
{
public:
    explicit MBoxEffectChain (int numChannels);
    ~MBoxEffectChain();

    int getNumChannels() const noexcept  { return m_numChannels; }

    void addPlugin (std::unique_ptr<juce::AudioPluginInstance> plugin, bool bypassed);
    int getNumPlugins() const noexcept  { return (int) m_plugins.size(); }
    juce::AudioPluginInstance* getPlugin (int index) const noexcept;

    // Can be called from any thread; takes effect on the next block
    void setBypassed (int index, bool bypassed) noexcept;

    // Chooses each plugin's channel layout, then calls prepareToPlay. maxBlockSize is the
    // largest block process() will ever be given.
    void prepare (double sampleRate, int maxBlockSize);

    // The plugins' total latency in samples, and the longest tail (capped at 10 s)
    int getLatencySamples() const noexcept;
    double getTailLengthSeconds() const noexcept;

    // Adds a delay so the chain's total latency is totalLatency. Call after prepare().
    void setCompensatedLatency (int totalLatency);

    // Processes a few seconds of silence, so plugins that initialise lazily (loading impulse
    // responses or models, allocating on first use) do it now rather than on first use live.
    void warmUp (double seconds);

    // Audio thread only. numFrames must not exceed the prepared maxBlockSize.
    void process (float* const* channels, int numFrames) noexcept;

private:
    struct Plugin
    {
        std::unique_ptr<juce::AudioPluginInstance> m_instance;
        std::atomic<bool> m_bypassed { false };
        int m_numInputs { 0 };
        int m_numOutputs { 0 };
    };

    void choosePluginLayout (Plugin& plugin);

    const int m_numChannels;
    std::vector<std::unique_ptr<Plugin>> m_plugins;
    juce::AudioBuffer<float> m_scratch;
    juce::MidiBuffer m_midi;
    int m_maxBlockSize { 0 };

    // Compensation delay: a ring buffer per channel holding exactly m_delayLength samples
    juce::AudioBuffer<float> m_delay;
    int m_delayLength { 0 };
    int m_delayPosition { 0 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MBoxEffectChain)
};
