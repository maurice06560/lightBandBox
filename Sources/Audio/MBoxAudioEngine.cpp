#include "MBoxAudioEngine.h"

//==============================================================================
// Retrieve the information about an audio interface: the exact # of input and output
// channels instead of forcing 2 inputs and 2 outputs
//
// Force the sample rate to 44100Hz (if the interface supports it)
// Force the buffer of sample per callback to 32, or the requested size (if the interface supports it)
//==============================================================================
namespace
{
    constexpr int preferredBufferSize = 32;

    // The setup to open a device with, plus the channel counts to request.
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

    // Probes a device without leaving it open. Empty names mean the type's default devices.
    // Returns the device's names and channel counts, plus 44100 Hz and the requested buffer
    // size where the device supports them. Returns nothing when the device can't be created.
    std::optional<PreferredDeviceSetup> probeDevice (juce::AudioIODeviceType& type, juce::String inputName,
                                                     juce::String outputName, int bufferSize)
    {
        type.scanForDevices();
        const auto inputNames  = type.getDeviceNames (true);
        const auto outputNames = type.getDeviceNames (false);

        if (inputName.isEmpty() && ! inputNames.isEmpty())
            inputName = getDefaultDeviceName (type, inputNames, true);

        if (outputName.isEmpty() && ! outputNames.isEmpty())
            outputName = getDefaultDeviceName (type, outputNames, false);

        if (inputName.isEmpty() && outputName.isEmpty())
            return std::nullopt;

        std::unique_ptr<juce::AudioIODevice> device (type.createDevice (outputName, inputName));

        if (device == nullptr)
            return std::nullopt;

        PreferredDeviceSetup preferred;
        preferred.setup.inputDeviceName  = inputName;
        preferred.setup.outputDeviceName = outputName;
        preferred.numInputChannels       = device->getInputChannelNames().size();
        preferred.numOutputChannels      = device->getOutputChannelNames().size();

        if (device->getAvailableSampleRates().contains (supportedSampleRate))
            preferred.setup.sampleRate = supportedSampleRate;
        else
            juce::Logger::writeToLog ("MBoxAudioEngine: device does not support 44100 Hz, using the device default. WAV files will play at the wrong speed");

        const auto wantedBufferSize = bufferSize > 0 ? bufferSize : preferredBufferSize;

        if (device->getAvailableBufferSizes().contains (wantedBufferSize))
            preferred.setup.bufferSize = wantedBufferSize;
        else
            juce::Logger::writeToLog ("MBoxAudioEngine: device does not support " + juce::String (wantedBufferSize)
                                      + "-sample buffers, using the device default");

        return preferred;
    }

    // Probes the system's default audio device: the first device type that has one
    std::optional<PreferredDeviceSetup> getPreferredDeviceSetup (juce::AudioDeviceManager& deviceManager)
    {
        for (auto* type : deviceManager.getAvailableDeviceTypes())
            if (auto preferred = probeDevice (*type, {}, {}, 0))
                return preferred;

        return std::nullopt;
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
// A thread standing in for an audio device: it calls the renderer with one chunk of silent
// input every chunk duration, so playback advances in real time without hardware
//==============================================================================
class MBoxAudioEngine::NullDevice final : private juce::Thread
{
public:
    explicit NullDevice (MBoxRenderer& renderer)
        : juce::Thread ("MBox null audio device"), m_renderer (renderer)
    {
        m_renderer.prepare (supportedSampleRate);
        m_inputs.setSize (2, MBoxRenderer::maxChunkSize);
        m_outputs.setSize (numberOfMixBus * 2, MBoxRenderer::maxChunkSize);
        m_inputs.clear();
        startThread (juce::Thread::Priority::high);
    }

    ~NullDevice() override
    {
        stopThread (2000);
    }

private:
    void run() override
    {
        const auto chunkMs = 1000.0 * MBoxRenderer::maxChunkSize / supportedSampleRate;
        auto nextTime = juce::Time::getMillisecondCounterHiRes();

        while (! threadShouldExit())
        {
            m_renderer.process (m_inputs.getArrayOfReadPointers(), m_inputs.getNumChannels(),
                                m_outputs.getArrayOfWritePointers(), m_outputs.getNumChannels(),
                                MBoxRenderer::maxChunkSize);

            nextTime += chunkMs;
            const auto waitMs = nextTime - juce::Time::getMillisecondCounterHiRes();

            if (waitMs > 0)
                wait ((int) std::ceil (waitMs));
            else
                nextTime = juce::Time::getMillisecondCounterHiRes();  // fell behind: don't try to catch up
        }
    }

    MBoxRenderer& m_renderer;
    juce::AudioBuffer<float> m_inputs, m_outputs;
};

//==============================================================================
// MBoxAudioEngine contructor and destructor
//==============================================================================
MBoxAudioEngine::MBoxAudioEngine() = default;

MBoxAudioEngine::~MBoxAudioEngine()
{
    m_nullDevice.reset();
    m_deviceManager.removeAudioCallback (this);
    m_deviceManager.closeAudioDevice();
}

void MBoxAudioEngine::startWithoutDevice()
{
    if (m_nullDevice == nullptr)
        m_nullDevice = std::make_unique<NullDevice> (m_renderer);
}

//==============================================================================
// Initialize device manager: the saved device first, then the system default
//==============================================================================
juce::Result MBoxAudioEngine::initialise (const MBoxDeviceSettings& savedSettings)
{
    m_deviceManager.addAudioCallback (this);

    if (! savedSettings.isSystemDefault())
    {
        // Lists the device types, which setDevice needs, without opening anything
        m_deviceManager.getAvailableDeviceTypes();

        const auto result = setDevice (savedSettings);

        if (result.wasOk())
            return result;

        juce::Logger::writeToLog ("MBoxAudioEngine: [Engine initialization]: saved device unavailable ("
                                  + result.getErrorMessage() + "), using the system default");
    }

    // Don't guess the number of inputs and outputs of the audio interface. Retrieve the
    // correct numbers from the default device, and open that device with the preferred
    // settings in one step so the hardware is only opened once.
    const auto preferred = getPreferredDeviceSetup (m_deviceManager);

    if (! preferred.has_value())
    {
        juce::Logger::writeToLog ("MBoxAudioEngine: [Engine initialization]: ERROR - no audio device found");
        return juce::Result::fail ("No audio device found");
    }

    juce::String startupError = m_deviceManager.initialise (preferred->numInputChannels,
                                                 preferred->numOutputChannels,
                                                 nullptr,
                                                 true,
                                                 juce::String(),
                                                 &preferred->setup);

    if (startupError.isNotEmpty())
    {
        juce::Logger::writeToLog("MBoxAudioEngine: [Engine initialization]: ERROR - " + startupError);
        return juce::Result::fail (startupError);
    }

    writeAudioEngineSetupToLogs();
    juce::Logger::writeToLog("MBoxAudioEngine: [Engine initialization]: SUCCESS");
    return juce::Result::ok();
}

//==============================================================================
// Devices
//==============================================================================
juce::StringArray MBoxAudioEngine::getAvailableDeviceTypes()
{
    juce::StringArray names;

    for (auto* type : m_deviceManager.getAvailableDeviceTypes())
        names.add (type->getTypeName());

    return names;
}

juce::StringArray MBoxAudioEngine::getAvailableDeviceNames (const juce::String& typeName, bool forInput)
{
    for (auto* type : m_deviceManager.getAvailableDeviceTypes())
    {
        if (type->getTypeName() == typeName)
        {
            type->scanForDevices();
            return type->getDeviceNames (forInput);
        }
    }

    return {};
}

MBoxDeviceSettings MBoxAudioEngine::getCurrentDeviceSettings() const
{
    MBoxDeviceSettings settings;
    auto* device = m_deviceManager.getCurrentAudioDevice();

    if (device == nullptr)
        return settings;

    const auto setup = m_deviceManager.getAudioDeviceSetup();
    settings.m_typeName = m_deviceManager.getCurrentAudioDeviceType();
    settings.m_inputDeviceName = setup.inputDeviceName;
    settings.m_outputDeviceName = setup.outputDeviceName;
    settings.m_bufferSize = device->getCurrentBufferSizeSamples();
    return settings;
}

juce::StringArray MBoxAudioEngine::getChannelNames (bool input) const
{
    if (auto* device = m_deviceManager.getCurrentAudioDevice(); device != nullptr && m_nullDevice == nullptr)
        return input ? device->getInputChannelNames() : device->getOutputChannelNames();

    juce::StringArray names;

    for (int channel = 1; channel <= (input ? 2 : numberOfMixBus * 2); ++channel)
        names.add ((input ? "Input " : "Output ") + juce::String (channel));

    return names;
}

juce::Array<int> MBoxAudioEngine::getAvailableBufferSizes() const
{
    if (auto* device = m_deviceManager.getCurrentAudioDevice())
        return device->getAvailableBufferSizes();

    return {};
}

double MBoxAudioEngine::getSampleRate() const
{
    return m_sampleRate;
}

juce::Result MBoxAudioEngine::setDevice (const MBoxDeviceSettings& settings)
{
    m_nullDevice.reset();

    if (settings.m_typeName.isNotEmpty() && settings.m_typeName != m_deviceManager.getCurrentAudioDeviceType())
    {
        if (! getAvailableDeviceTypes().contains (settings.m_typeName))
            return juce::Result::fail ("Unknown device type " + settings.m_typeName);

        m_deviceManager.setCurrentAudioDeviceType (settings.m_typeName, true);
    }

    return openDevice (settings.m_inputDeviceName, settings.m_outputDeviceName, settings.m_bufferSize);
}

juce::Result MBoxAudioEngine::setBufferSize (int bufferSize)
{
    auto* device = m_deviceManager.getCurrentAudioDevice();

    if (device == nullptr)
        return juce::Result::fail ("No audio device is open");

    if (! device->getAvailableBufferSizes().contains (bufferSize))
        return juce::Result::fail ("The device doesn't support " + juce::String (bufferSize) + "-sample buffers");

    auto setup = m_deviceManager.getAudioDeviceSetup();
    setup.bufferSize = bufferSize;
    const auto error = m_deviceManager.setAudioDeviceSetup (setup, true);
    return error.isEmpty() ? juce::Result::ok() : juce::Result::fail (error);
}

juce::Result MBoxAudioEngine::openDevice (const juce::String& inputName, const juce::String& outputName, int bufferSize)
{
    auto* type = m_deviceManager.getCurrentDeviceTypeObject();

    if (type == nullptr)
        return juce::Result::fail ("No audio device type available");

    if ((inputName.isNotEmpty() && ! type->getDeviceNames (true).contains (inputName))
        || (outputName.isNotEmpty() && ! type->getDeviceNames (false).contains (outputName)))
    {
        type->scanForDevices();

        if ((inputName.isNotEmpty() && ! type->getDeviceNames (true).contains (inputName))
            || (outputName.isNotEmpty() && ! type->getDeviceNames (false).contains (outputName)))
            return juce::Result::fail ("Device not connected: " + (outputName.isNotEmpty() ? outputName : inputName));
    }

    // ASIO drivers can only be opened once at a time, so the probe can't run while the
    // current device is open
    m_deviceManager.closeAudioDevice();

    const auto preferred = probeDevice (*type, inputName, outputName, bufferSize);

    if (! preferred.has_value())
        return juce::Result::fail ("Can't open the device " + (outputName.isNotEmpty() ? outputName : inputName));

    auto setup = preferred->setup;
    setup.useDefaultInputChannels = false;
    setup.useDefaultOutputChannels = false;
    setup.inputChannels.setRange (0, preferred->numInputChannels, true);
    setup.outputChannels.setRange (0, preferred->numOutputChannels, true);

    const auto error = m_deviceManager.setAudioDeviceSetup (setup, true);

    if (error.isNotEmpty())
        return juce::Result::fail (error);

    writeAudioEngineSetupToLogs();
    return juce::Result::ok();
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
    m_renderer.process (inputChannelData, numInputChannels, outputChannelData, numOutputChannels, numSamples);
}

void MBoxAudioEngine::audioDeviceAboutToStart (juce::AudioIODevice* device)
{
    // Called before the first callback, and again whenever the device restarts (for example after a sample rate change)
    m_sampleRate = device->getCurrentSampleRate();
    m_renderer.prepare (m_sampleRate);
}

void MBoxAudioEngine::audioDeviceStopped()
{
}
