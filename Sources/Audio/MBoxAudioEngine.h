#pragma once

#include <juce_audio_devices/juce_audio_devices.h>
#include <juce_audio_formats/juce_audio_formats.h>

#include "../Model/MBoxSong.h"
#include "../Model/MBoxClipStreamer.h"

using ClipList = std::vector<std::unique_ptr<MBoxClipStreamer>>;

enum class AudioState
{
    loading,
    stopped,
    playing,
    paused
};

class MBoxAudioEngine final : private juce::AudioIODeviceCallback
{
public:
    MBoxAudioEngine();
    ~MBoxAudioEngine() override;

    /** Prepares the engine to play the given song from the given playlist. */
    void selectSong(const juce::String& playlistName, const MBoxSong* song);

    // =============================================================================
    // Transport
    // =============================================================================
    /** Starts playing from the current playhead. Ignored while a song is loading or when no song is selected. */
    void play();
    /** Pauses playback and keeps the playhead where it is. Only has an effect while playing. */
    void pause();
    /** Stops playback and moves the playhead back to the start of the song. Ignored while a song is loading.
        Message thread only (it logs); the audio callback stops on its own when it reaches the right locator. */
    void stop();

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

    /** Converts a position in the selected song to an absolute tick count from the start of the song ({1,1,1,0} is tick 0).
        A beat is always Bar::ticksPerBeat (960) ticks, and each bar holds as many beats as the numerator
        of the signature in effect, so the song's signature grid is walked bar by bar.
        Signature changes are applied from the start of the bar they are set in. Returns 0 if no song is selected. */
    std::int64_t convertBarToTick(const Bar& bar) const;

    /** Converts an absolute tick count from the start of the selected song (tick 0 is 0 seconds) to seconds.
        Each beat (Bar::ticksPerBeat ticks) lasts 60 / tempo seconds, with the tempo taken from the song's
        tempo grid, so tempo changes before the given tick are taken into account. Returns 0 if no song is selected. */
    double convertTickToSeconds(double ticks) const;

    /** Returns the tempo (in BPM) in effect at the given tick of the selected song: the last tempo change at or
        before it. Positions before the first change use the song's first tempo. Returns 120 if no song is selected.
        Uses m_tempoTicks (built in selectSong), so it is a binary search and safe to call from the audio callback. */
    int getTempoForTick(double tick) const;

    /** Returns the tick of the first tempo change strictly after the given tick, or +infinity if there is none.
        Same data and thread safety as getTempoForTick. */
    double getNextTempoChangeTick(double tick) const;

    // =============================================================================
    // Variables
    // =============================================================================
    juce::AudioDeviceManager m_deviceManager;

    // The playhead keeps fractional ticks: one audio block is often only a tick or two long,
    // so whole ticks would lose most of the movement to rounding.
    std::atomic<double> m_playhead{0.0};                    // playhead in fractional ticks
    std::atomic<std::int64_t> m_leftLocator{0};             // m_leftLocator in ticks
    std::atomic<std::int64_t> m_rightLocator{0};            // m_rightLocator in ticks
    std::atomic<AudioState> m_state{AudioState::stopped};
    double m_sampleRate { 44100.0 };                        // set in audioDeviceAboutToStart

    // The selected song's tempo changes as (tick, BPM), sorted by tick. Built in selectSong under m_clipsLock,
    // so the audio callback (which only reads it while holding that lock) never sees it half-written.
    std::vector<std::pair<double, int>> m_tempoTicks;

    ClipList m_clips;
    juce::SpinLock m_clipsLock;

    // =============================================================================
    // Audio file reader
    // =============================================================================
    juce::AudioFormatManager m_formatManager;   // To configure the supported audio files 
    // TO DO: Need to implement the playlist,song and track classes to define the full path
    juce::File m_audioFolder{"/Users/mmarin/OneBox/"};
    juce::TimeSliceThread m_readAheadThread{"MBox read-ahead"};

    const MBoxSong* m_song = nullptr;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MBoxAudioEngine)
};
