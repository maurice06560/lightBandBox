#include "MBoxAudioEngine.h"

//==============================================================================
// Retrieve the information about all Audio interface available, match the system's
// selected one and retrieve the exact # of input and output instead of forcing 2
// inputs and 2 outputs
//
// Force the sample rate to 44100Hz (if the interface supports it)
// Force the buffer of sample per callback to 32 (if the interface supports it)
//==============================================================================
namespace
{
    // The setup the engine opens the default device with, plus the channel counts to request.
    struct PreferredDeviceSetup
    {
        juce::AudioDeviceManager::AudioDeviceSetup setup;
        int numInputChannels  = 0;
        int numOutputChannels = 0;
    };

    // Returns the name of the type's default device in the given direction. The name list must not be empty.
    juce::String getDefaultDeviceName (juce::AudioIODeviceType& type, const juce::StringArray& names, bool forInput)
    {
        const auto index = type.getDefaultDeviceIndex (forInput);
        return names[juce::isPositiveAndBelow (index, names.size()) ? index : 0];
    }

    // Probes the system's default audio device without leaving it open. Returns the device's names
    // and channel counts, plus 44100 Hz and a 32-sample buffer where the device supports them.
    PreferredDeviceSetup getPreferredDeviceSetup (juce::AudioDeviceManager& deviceManager)
    {
        constexpr double preferredSampleRate = 44100.0;
        constexpr int preferredBufferSize = 32;

        PreferredDeviceSetup preferred;

        for (auto* type : deviceManager.getAvailableDeviceTypes())
        {
            const auto inputNames  = type->getDeviceNames (true);
            const auto outputNames = type->getDeviceNames (false);

            if (inputNames.isEmpty() && outputNames.isEmpty())
                continue;

            const auto inputName  = inputNames.isEmpty()  ? juce::String() : getDefaultDeviceName (*type, inputNames, true);
            const auto outputName = outputNames.isEmpty() ? juce::String() : getDefaultDeviceName (*type, outputNames, false);

            std::unique_ptr<juce::AudioIODevice> device (type->createDevice (outputName, inputName));

            if (device == nullptr)
                continue;

            preferred.setup.inputDeviceName  = inputName;
            preferred.setup.outputDeviceName = outputName;
            preferred.numInputChannels       = device->getInputChannelNames().size();
            preferred.numOutputChannels      = device->getOutputChannelNames().size();

            if (device->getAvailableSampleRates().contains (preferredSampleRate))
                preferred.setup.sampleRate = preferredSampleRate;
            else
                juce::Logger::writeToLog ("MBoxAudioEngine: device does not support 44100 Hz, using the device default");

            if (device->getAvailableBufferSizes().contains (preferredBufferSize))
                preferred.setup.bufferSize = preferredBufferSize;
            else
                juce::Logger::writeToLog ("MBoxAudioEngine: device does not support 32-sample buffers, using the device default");

            return preferred;
        }

        return preferred;
    }
}

//==============================================================================
// Write engine information
//==============================================================================
void MBoxAudioEngine::writeAudioEngineSetupToLogs()
{
    const auto setup = m_deviceManager.getAudioDeviceSetup();
    juce::Logger::writeToLog("MBoxAudioEngine: input device: " + setup.inputDeviceName);
    juce::Logger::writeToLog("MBoxAudioEngine: output device: " + setup.outputDeviceName);

    auto* device = m_deviceManager.getCurrentAudioDevice();

    if (device == nullptr)
    {
        juce::Logger::writeToLog("MBoxAudioEngine: no audio device is open");
        return;
    }

    juce::Logger::writeToLog("MBoxAudioEngine: sample rate: " + juce::String (device->getCurrentSampleRate(), 1) + " Hz");
    juce::Logger::writeToLog("MBoxAudioEngine: buffer size: " + juce::String (device->getCurrentBufferSizeSamples()) + " samples");

    juce::StringArray bufferSizeNames;

    for (const auto size : device->getAvailableBufferSizes())
        bufferSizeNames.add (juce::String (size));

    juce::Logger::writeToLog("MBoxAudioEngine: supported buffer sizes: " + bufferSizeNames.joinIntoString (", ") + " samples");

    juce::StringArray sampleRateNames;

    for (const auto rate : device->getAvailableSampleRates())
        sampleRateNames.add (juce::String (rate, 0));

    juce::Logger::writeToLog("MBoxAudioEngine: supported sample rates: " + sampleRateNames.joinIntoString (", ") + " Hz");
    juce::Logger::writeToLog("MBoxAudioEngine: input channels: " + juce::String (device->getActiveInputChannels().countNumberOfSetBits()));
    juce::Logger::writeToLog("MBoxAudioEngine: output channels: " + juce::String (device->getActiveOutputChannels().countNumberOfSetBits()));
}

//==============================================================================
// MBoxAudioEngine contructor: Initilize device manager
//==============================================================================
MBoxAudioEngine::MBoxAudioEngine()
{
    // Don't guess the number of inputs and outputs of the audio interface. Retrieve the
    // correct numbers from the default device, and open that device with the preferred
    // settings in one step so the hardware is only opened once.
    const auto preferred = getPreferredDeviceSetup (m_deviceManager);

    juce::String startupError = m_deviceManager.initialise (preferred.numInputChannels,
                                                 preferred.numOutputChannels,
                                                 nullptr,
                                                 true,
                                                 juce::String(),
                                                 &preferred.setup);

    if (startupError.isNotEmpty())
    {
        juce::Logger::writeToLog ("MBoxAudioEngine: [Engine initialization]: ERROR - " + startupError);
    }
    else
    {
        writeAudioEngineSetupToLogs();
        juce::Logger::writeToLog ("MBoxAudioEngine: [Engine initialization]: SUCCESS");
    }

    m_deviceManager.addAudioCallback (this);
}

MBoxAudioEngine::~MBoxAudioEngine()
{
    m_deviceManager.removeAudioCallback (this);
    m_deviceManager.closeAudioDevice();
}

//==============================================================================
// Main JUCE callback
//==============================================================================
void MBoxAudioEngine::audioDeviceIOCallbackWithContext (const float* const* inputChannelData,
                                                        int numInputChannels,
                                                        float* const* outputChannelData,
                                                        int numOutputChannels,
                                                        int numSamples,
                                                        const juce::AudioIODeviceCallbackContext&)
{
    for (int channel = 0; channel < numOutputChannels; ++channel)
        if (outputChannelData[channel] != nullptr)
            juce::FloatVectorOperations::clear (outputChannelData[channel], numSamples);

    // Route input 1 -> output 1 and input 2 -> output 2.
    // Will need a mixBus to handle the targetted outputs
    constexpr int numRoutedChannels = 2;
    const auto numChannelsToCopy = juce::jmin (numRoutedChannels, numInputChannels, numOutputChannels);

    for (int channel = 0; channel < numChannelsToCopy; ++channel)
        if (inputChannelData[channel] != nullptr && outputChannelData[channel] != nullptr)
            juce::FloatVectorOperations::copy (outputChannelData[channel], inputChannelData[channel], numSamples);
}

void MBoxAudioEngine::audioDeviceAboutToStart (juce::AudioIODevice*)
{
}

void MBoxAudioEngine::audioDeviceStopped()
{
}
