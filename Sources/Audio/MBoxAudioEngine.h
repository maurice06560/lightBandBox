#pragma once

#include "MBoxRenderer.h"

#include <juce_audio_devices/juce_audio_devices.h>

// Owns the audio device and forwards every audio block to the renderer.
// The callback is implemented privately so only the AudioDeviceManager can call it.
// Every method except the callback must be called on the message thread.
class MBoxAudioEngine final : private juce::AudioIODeviceCallback
{
public:
    MBoxAudioEngine();

    // Unregisters the callback before closing the device, so no callback can run on a
    // partly destroyed object.
    ~MBoxAudioEngine() override;

    // Opens the saved device when there is one and it's connected, otherwise the system's
    // default device, with all of its physical inputs and outputs, at 44100 Hz and the saved
    // (or a 32-sample) buffer size where supported. Registers the callback either way.
    //
    // The hardware is opened only once: each device is probed first (without staying open)
    // to learn its real channel counts, and those counts and settings are opened in one call.
    // Requires the JUCE MessageManager to exist (see MBoxCoreAudio). Returns an error when no
    // device could be opened; the engine then runs without audio.
    juce::Result initialise (const MBoxDeviceSettings& savedSettings);

    // Runs the renderer from a background thread in real time, with silent inputs and
    // discarded outputs, as if a 2-in/6-out device were open. Used when there is no audio
    // device (tests, or a device failure), so the transport still moves and commands are
    // still consumed. Opening a real device with setDevice() stops it.
    void startWithoutDevice();
    bool isRunningWithoutDevice() const noexcept  { return m_nullDevice != nullptr; }

    MBoxRenderer& getRenderer() noexcept  { return m_renderer; }

    //==========================================================================
    // Devices

    juce::StringArray getAvailableDeviceTypes();
    juce::StringArray getAvailableDeviceNames (const juce::String& typeName, bool forInput);

    // The open device, as it would be saved. Empty names when no device is open.
    MBoxDeviceSettings getCurrentDeviceSettings() const;
    juce::Array<int> getAvailableBufferSizes() const;
    // The names of the open device's input or output channels (the engine opens all of them).
    // Without a device: the null device's ("Input 1", "Input 2", "Output 1"...).
    juce::StringArray getChannelNames (bool input) const;
    double getSampleRate() const;

    // Opens another device. Empty names mean the type's default devices, an empty type the
    // current type, and a buffer size of 0 the preferred one. Fails, leaving no device open,
    // when the device can't be opened.
    juce::Result setDevice (const MBoxDeviceSettings& settings);

    // Reopens the current device with another buffer size, which must be one it supports
    juce::Result setBufferSize (int bufferSize);

private:
    // Real-time audio thread entry point, called by the device for every block.
    // - inputChannelData / outputChannelData: one non-interleaved float array per active
    //   channel. A channel that wasn't enabled when the device was opened has a null
    //   pointer and must be skipped.
    // - numSamples: the number of frames per channel in this block. It can vary from one
    //   call to the next, and is not guaranteed to equal the requested buffer size.
    // Output buffers start with undefined contents, so every output channel must be fully
    // written (zeros for silence) before returning. Must not allocate, lock, log, or do
    // any file or network I/O.
    void audioDeviceIOCallbackWithContext (const float* const* inputChannelData,
                                           int numInputChannels,
                                           float* const* outputChannelData,
                                           int numOutputChannels,
                                           int numSamples,
                                           const juce::AudioIODeviceCallbackContext& context) override;

    // Called on a non-audio thread before the first callback, and again whenever the
    // device restarts (sample rate, buffer size or device change). Prepares the renderer
    // for the device's sample rate.
    void audioDeviceAboutToStart (juce::AudioIODevice* device) override;

    // Called when the device stops or is closed.
    void audioDeviceStopped() override;

    // =============================================================================
    // Private additional methods
    // =============================================================================
    // Logs the open device's names, current and supported sample rates and buffer sizes,
    // and active channel counts. Must not be called from the audio thread.
    void writeAudioEngineSetupToLogs();

    // Opens the given device of the current type with all its channels
    juce::Result openDevice (const juce::String& inputName, const juce::String& outputName, int bufferSize);

    // =============================================================================
    // Variables
    // =============================================================================
    class NullDevice;

    juce::AudioDeviceManager m_deviceManager;
    MBoxRenderer m_renderer;
    std::unique_ptr<NullDevice> m_nullDevice;

    double m_sampleRate { 44100.0 }; // set in audioDeviceAboutToStart

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MBoxAudioEngine)
};
