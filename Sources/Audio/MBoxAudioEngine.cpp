#include "MBoxAudioEngine.h"
#include "../Model/MBoxAudioTrack.h"

#include <algorithm>
#include <cmath>
#include <limits>

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
// Initialize the engine to play a song
//==============================================================================
void MBoxAudioEngine::selectSong(const juce::String& playlistName, const MBoxSong* song) {

    // The callback outputs silence from here on. m_song is only read on the message thread.
    m_state.store(AudioState::loading, std::memory_order_release);
    m_song = song;

    // Everything slow (opening files, walking the grids) is done into locals without the lock,
    // which is only taken for the swap at the end.
    ClipList clips;

    const auto mediaFolder = getMediaFolder (playlistName, m_song->getName());

    for (auto* track : m_song->getTracks()) {

        // Only audio tracks have a file to read
        if (const auto* audioTrack = dynamic_cast<const MBoxAudioTrack*> (track)) {

            const auto file = mediaFolder.getChildFile (audioTrack->getFileName());
            std::unique_ptr<juce::AudioFormatReader> reader (m_formatManager.createReaderFor (file));

            juce::Logger::writeToLog ("MBoxAudioEngine: Reading file: " + file.getFullPathName());
            if (reader == nullptr) {
                juce::Logger::writeToLog ("MBoxAudioEngine: cannot read audio file: " + file.getFullPathName());
                continue;
            }

            const auto startTicks = convertBarToTick(audioTrack->getStartAudio());
            const auto startSeconds = convertTickToSeconds(static_cast<double> (startTicks));

            clips.push_back (std::make_unique<MBoxClipStreamer> (std::move (reader), startSeconds));
        }
    }

    // Convert the tempo changes to ticks once, so the audio callback doesn't walk the bar grids.
    // A tempo of 0 or less would divide by zero in the callback, so such changes are dropped.
    std::vector<std::pair<double, int>> tempoTicks;
    std::vector<double> tempoSeconds;

    for (const auto& [position, bpm] : m_song->getTempoGrid())
    {
        if (bpm <= 0)
        {
            juce::Logger::writeToLog ("MBoxAudioEngine: ignoring invalid tempo " + juce::String (bpm) + " in song " + m_song->getName());
            continue;
        }

        const auto tick = (double) convertBarToTick (position);

        // Seconds at this change: the previous change's seconds plus the time spent at the previous tempo
        tempoSeconds.push_back (tempoTicks.empty() ? convertTickToSeconds (tick)
                                                   : tempoSeconds.back() + (tick - tempoTicks.back().first) * 60.0
                                                                               / ((double) tempoTicks.back().second * Bar::ticksPerBeat));
        tempoTicks.emplace_back (tick, bpm);
    }

    // The locators start on the song's start and end positions, and the playhead on the left locator
    const auto leftLocator  = convertBarToTick (m_song->getSongStartPosition());
    const auto rightLocator = convertBarToTick (m_song->getSongEndPosition());

    {
        const juce::SpinLock::ScopedLockType lock (m_clipsLock);

        // The previous song's clips and tempos end up in the locals, so they are freed after the lock is released
        std::swap (m_clips, clips);
        std::swap (m_tempoTicks, tempoTicks);
        std::swap (m_tempoSeconds, tempoSeconds);

        m_leftLocator.store (leftLocator, std::memory_order_release);
        m_rightLocator.store (rightLocator, std::memory_order_release);
        m_playhead.store (static_cast<double> (leftLocator), std::memory_order_release);
    }

    // The previous song's clips must leave the read-ahead thread before they are destroyed (this waits
    // for a slice in progress), and the new ones start streaming as soon as the callback seeks them.
    for (auto& clip : clips)
        m_readAheadThread.removeTimeSliceClient (clip.get());

    clips.clear();

    for (auto& clip : m_clips)
        m_readAheadThread.addTimeSliceClient (clip.get());

    m_state.store(AudioState::stopped, std::memory_order_release);
}

juce::File MBoxAudioEngine::getMediaFolder (const juce::String& playlistName, const juce::String& songName) const
{
    return m_audioFolder.getChildFile ("playlists")
                        .getChildFile (playlistName)
                        .getChildFile ("songs")
                        .getChildFile (songName)
                        .getChildFile ("tracks/media");
}

//==============================================================================
// Transport
//==============================================================================
void MBoxAudioEngine::play()
{
    const auto state = m_state.load (std::memory_order_acquire);

    if (m_song == nullptr || (state != AudioState::stopped && state != AudioState::paused))
        return;

        // Wall-clock time with milliseconds, e.g. "13 Nov 2026 17:42:05.123"
   // juce::Logger::writeToLog ("MBoxApplication: play at " + juce::Time::getCurrentTime().toString (true, true, true, true));
    //juce::Logger::writeToLog("MBoxAudioEngine: Play");

    m_state.store (AudioState::playing, std::memory_order_release);
}

void MBoxAudioEngine::pause()
{
    if (m_state.load (std::memory_order_acquire) != AudioState::playing)
        return;

    //juce::Logger::writeToLog("MBoxAudioEngine: Pause");

    m_state.store (AudioState::paused, std::memory_order_release);
}

void MBoxAudioEngine::stop()
{
    if (m_state.load (std::memory_order_acquire) == AudioState::loading)
        return;

    //juce::Logger::writeToLog("MBoxAudioEngine: Stop");
    // Wall-clock time with milliseconds, e.g. "13 Nov 2026 17:42:05.123"
    //juce::Logger::writeToLog ("MBoxApplication: stop at " + juce::Time::getCurrentTime().toString (true, true, true, true));

    m_state.store (AudioState::stopped, std::memory_order_release);
    m_playhead.store (static_cast<double>(m_leftLocator), std::memory_order_release);
}

//==============================================================================
// MBoxAudioEngine contructor: Initilize device manager
//==============================================================================
MBoxAudioEngine::MBoxAudioEngine()
{
    // Only support WAV files
    // Start the TimeSliceThread
    m_formatManager.registerFormat(new juce::WavAudioFormat(), true); // true = default format
    m_readAheadThread.startThread (juce::Thread::Priority::high);

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
        juce::Logger::writeToLog("MBoxAudioEngine: [Engine initialization]: ERROR - " + startupError);
    }
    else
    {
        writeAudioEngineSetupToLogs();
        juce::Logger::writeToLog("MBoxAudioEngine: [Engine initialization]: SUCCESS");
    }

    m_deviceManager.addAudioCallback(this);
}

//==============================================================================
// MBoxAudioEngine destructor
//==============================================================================
MBoxAudioEngine::~MBoxAudioEngine()
{
    m_deviceManager.removeAudioCallback (this);
    m_deviceManager.closeAudioDevice();
    // Waits up to 2000 ms (2 seconds) to stop the thread, otherwise JUCE kills it
    m_readAheadThread.stopThread (2000);
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

    // m_clipsLock is only held while a new song is being selected: output silence for this block
    const juce::SpinLock::ScopedTryLockType lock (m_clipsLock);

    if (! lock.isLocked())
        return;

    const auto state = m_state.load (std::memory_order_relaxed);

    if (state == AudioState::loading)
        return;

    // Stopped or paused: keep the clips cued on the playhead, so they are ready the moment play starts
    if (state != AudioState::playing)
    {
        seekClips (m_playhead.load (std::memory_order_relaxed));
        return;
    }

    // Work on a local copy of the playhead and publish it once at the end of the block (see below)
    const auto blockStartPlayhead = m_playhead.load (std::memory_order_relaxed);
    const auto right              = (double) m_rightLocator.load (std::memory_order_relaxed);
    auto playhead                 = blockStartPlayhead;
    auto reachedEnd               = false;
    int offset                    = 0;

    // The playhead only jumps between blocks (stop, end of song, song load, rate change), never
    // inside one, so the clips only need to be checked once, here
    seekClips (blockStartPlayhead);

    // The block is split into segments that each have a single tempo: a segment ends at the next
    // tempo change, at the right locator, or at the end of the block, whichever comes first.
    while (offset < numSamples)
    {
        const auto ticksPerSample = getTempoForTick (playhead) * Bar::ticksPerBeat / (60.0 * m_sampleRate);

        // Samples whose start is before the given tick. The tiny epsilon absorbs the rounding error that
        // builds up in the playhead, which would otherwise turn e.g. 123.0000000001 into 124 samples.
        const auto samplesBefore = [&] (double tick) { return (int) std::ceil (juce::jmax (0.0, tick - playhead) / ticksPerSample - 1e-6); };

        const auto samplesToRight       = samplesBefore (right);
        const auto nextTempoChange      = getNextTempoChangeTick (playhead);
        // At least one sample per segment, so a tempo change a hair after the playhead can't stall the loop
        const auto samplesToTempoChange = nextTempoChange < right ? juce::jmax (1, samplesBefore (nextTempoChange)) : samplesToRight;
        const auto segment              = juce::jmin (numSamples - offset, samplesToRight, samplesToTempoChange);

        // ===========================================================================
        // Route input 1 -> output 1 and input 2 -> output 2.
        // Will need a mixBus to handle the targetted outputs
        constexpr int numRoutedChannels = 2;
        const auto numChannelsToCopy = juce::jmin (numRoutedChannels, numInputChannels, numOutputChannels);

        for (int channel = 0; channel < numChannelsToCopy; ++channel)
            if (inputChannelData[channel] != nullptr && outputChannelData[channel] != nullptr)
                juce::FloatVectorOperations::copy (outputChannelData[channel] + offset, inputChannelData[channel] + offset, segment);

        // Mix the clips on top of the input: each clip adds [offset, offset + segment)
        for (auto& clip : m_clips)
            clip->addTo (outputChannelData, numOutputChannels, offset, segment);

        playhead += segment * ticksPerSample;
        offset   += segment;

        if (segment == samplesToRight)
        {
            reachedEnd = true;                      // the rest of the block stays silent
            break;
        }
    }

    // Reaching the right locator stops and rewinds to the left locator. This is done here rather than
    // by calling stop(), which logs (allocation + file I/O) and must stay on the message thread.
    const auto newPlayhead = reachedEnd ? (double) m_leftLocator.load (std::memory_order_relaxed) : playhead;

    // Only publish if nobody moved the playhead during the block. If stop() (or a future seek) ran on the
    // message thread meanwhile, its position wins and this block's advance is dropped.
    auto expected = blockStartPlayhead;

    if (m_playhead.compare_exchange_strong (expected, newPlayhead, std::memory_order_relaxed) && reachedEnd)
        m_state.store (AudioState::stopped, std::memory_order_release);
}

void MBoxAudioEngine::audioDeviceAboutToStart (juce::AudioIODevice* device)
{
    // Called before the first callback, and again whenever the device restarts (for example after a sample rate change)
    m_sampleRate = device->getCurrentSampleRate();
}

void MBoxAudioEngine::audioDeviceStopped()
{
}

//==============================================================================
// Song position conversions
//==============================================================================
std::int64_t MBoxAudioEngine::convertBarToTick (const Bar& bar) const
{
    jassert (m_song != nullptr);

    return m_song != nullptr ? m_song->convertBarToTick (bar) : 0;
}

double MBoxAudioEngine::convertTickToSeconds (double ticks) const
{
    jassert (m_song != nullptr);

    return m_song != nullptr ? m_song->convertTickToSeconds (ticks) : 0.0;
}

int MBoxAudioEngine::getTempoForTick (double tick) const
{
    // Empty until a song is selected
    if (m_tempoTicks.empty())
        return 120;

    // First change strictly after tick; the one before it is the tempo in effect at tick
    const auto it = std::upper_bound (m_tempoTicks.begin(), m_tempoTicks.end(), tick,
                                      [] (double t, const auto& change) { return t < change.first; });

    // A tick before the first change uses the song's first tempo
    return it == m_tempoTicks.begin() ? it->second : std::prev (it)->second;
}

double MBoxAudioEngine::getNextTempoChangeTick (double tick) const
{
    const auto it = std::upper_bound (m_tempoTicks.begin(), m_tempoTicks.end(), tick,
                                      [] (double t, const auto& change) { return t < change.first; });

    return it == m_tempoTicks.end() ? std::numeric_limits<double>::infinity() : it->first;
}

double MBoxAudioEngine::getSecondsForTick (double tick) const
{
    // Empty until a song is selected
    if (m_tempoTicks.empty())
        return 0.0;

    const auto it = std::upper_bound (m_tempoTicks.begin(), m_tempoTicks.end(), tick,
                                      [] (double t, const auto& change) { return t < change.first; });

    // A tick before the first change uses the song's first tempo, counted from that change
    const auto index = (size_t) (it == m_tempoTicks.begin() ? 0 : std::distance (m_tempoTicks.begin(), it) - 1);
    const auto& [changeTick, bpm] = m_tempoTicks[index];

    return m_tempoSeconds[index] + (tick - changeTick) * 60.0 / ((double) bpm * Bar::ticksPerBeat);
}

void MBoxAudioEngine::seekClips (double tick)
{
    const auto songSample = (std::int64_t) std::llround (getSecondsForTick (tick) * m_sampleRate);

    for (auto& clip : m_clips)
        if (clip->needsSeek (songSample, m_sampleRate))
            clip->seek (songSample, m_sampleRate);
}
