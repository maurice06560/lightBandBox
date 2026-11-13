#pragma once

#include <juce_audio_devices/juce_audio_devices.h>

class MBoxAudioEngine final : private juce::AudioIODeviceCallback
{
public:
    MBoxAudioEngine();
    ~MBoxAudioEngine() override;

private:
    void audioDeviceIOCallbackWithContext (const float* const* inputChannelData,
                                           int numInputChannels,
                                           float* const* outputChannelData,
                                           int numOutputChannels,
                                           int numSamples,
                                           const juce::AudioIODeviceCallbackContext& context) override;
    void audioDeviceAboutToStart (juce::AudioIODevice* device) override;
    void audioDeviceStopped() override;

    // =============================================================================
    // Private additional methods
    // =============================================================================
    void writeAudioEngineSetupToLogs();

    // =============================================================================
    // Variables
    // =============================================================================
    juce::AudioDeviceManager m_deviceManager;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MBoxAudioEngine)
};
