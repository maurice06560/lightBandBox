#pragma once

#include "MBoxSongPlayer.h"

#include <array>
#include <atomic>

//==============================================================================
// Renders the audio of the loaded playlist. It knows nothing about audio devices, so tests
// can drive it with plain buffers; MBoxAudioEngine calls process() from the device callback.
//
// Threads:
// - The message thread loads playlists (publish()), sends transport commands, and changes
//   mix and bus values. None of these block: commands go through a lock-free FIFO, values
//   through atomics, and a new playlist player through an atomic pointer.
// - The audio thread calls process(), which never allocates, locks, logs or waits.
//
// Publishing: a new player replaces the current one at the start of the next block. The old
// one is freed later on the message thread (collectGarbage()), once the audio thread has
// confirmed it moved on, using a generation number stored in each player.
//
// Transport (all on the audio thread):
// - Stopped: silent, the position is the song's left locator.
// - play() from Stopped starts at "left locator - count-in", fading in over 10 ms.
// - pause() and stop() fade out over 10 ms first. Pause keeps the position; stop returns it
//   to the left locator. play() after a pause fades back in from where it stopped.
// - playFrom() starts at any position (with the count-in only when it's the left locator);
//   while playing, it fades out first, then jumps.
// - At the right locator: Stop (or the last song) fades out and stops; Continue jumps to the
//   next song at "its left locator - its count-in - this song's end delay", so the gap is
//   silent except for the count-in clicks.
// - Live inputs only sound while playing.
//
// Signal path, per block, for the current song:
//   track source (WAV / live input through its slot chain / info clicks)
//     -> for each bus: inserts -> volume (smoothed), mute/solo -> pan or balance -> bus sum
//   metronome clicks -> each bus at its metronome volume
//   bus -> master chain -> master volume and balance -> bus outputs
//   live input after its slot chain -> DI output
//   everything x transport fade gain
// All paths are aligned to the playlist's total plugin latency: clicks are rendered that
// many frames late, and every track's inserts are padded to it.
//==============================================================================
class MBoxRenderer
{
public:
    // Blocks are processed in chunks of at most this many frames; plugins are prepared with it
    static constexpr int maxChunkSize = 256;

    MBoxRenderer();
    ~MBoxRenderer();

    // Message thread, while the audio callback isn't running (device starting). Allocates the
    // scratch buffers and the click sounds.
    void prepare (double sampleRate);

    //==========================================================================
    // Message thread

    // Hands a new playlist player to the audio thread. The transport keeps its state; the song
    // index is clamped to the new player's songs. nullptr unloads (publishes an empty player).
    void publish (std::unique_ptr<MBoxPlaylistPlayer> player);

    // The most recently published player, for changing its mix values. Null before the first
    // publish; empty (no songs) after an unload. Message thread only.
    MBoxPlaylistPlayer* getPublishedPlayer() const noexcept  { return m_published.get(); }

    // Frees players the audio thread no longer uses. Call regularly (a timer).
    void collectGarbage();

    void play();
    void pause();
    void stop();
    // Plays the current song from a frame (from the start of bar 1): from "left locator -
    // count-in" when the frame is the left locator, otherwise from the frame itself.
    void playFrom (std::int64_t frame);
    // Selects a song (0-based) and puts the transport at its left locator, stopped. When
    // playing, fades out first.
    void selectSong (int songIndex);

    // Bus values: 1-based bus, output channels (second -1 for mono), volumes in dB; the
    // metronome is silent in a bus where metronomeOn is false
    void setBus (int bus, std::pair<int, int> outputs, double masterVolumeDb, double masterBalance, double metronomeVolumeDb,
                 bool metronomeOn = true);

    // Meters: the highest sample level (linear) since the last call, of a bus's output (after
    // its master) and of the metronome in it. Any thread. Tracks: MBoxMixState::takePeak().
    float takeMasterPeak (int bus) noexcept;
    float takeMetronomePeak (int bus) noexcept;

    struct Status
    {
        TransportState m_state { TransportState::Stopped };
        int m_songIndex { 0 };
        std::int64_t m_position { 0 };  // frames from the start of bar 1 of the current song
    };

    // Readable from any thread
    Status getStatus() const noexcept;

    //==========================================================================
    // Audio thread
    void process (const float* const* inputs, int numInputs, float* const* outputs, int numOutputs, int numFrames) noexcept;

private:
    enum class CommandType { Play, PlayFrom, Pause, Stop, SelectSong };

    struct Command
    {
        CommandType m_type;
        int m_value;
        std::int64_t m_frame;
    };

    enum class AfterFade { Nothing, Pause, Stop, PlayFrom };

    struct Bus
    {
        std::atomic<int> m_left { -1 }, m_right { -1 };
        std::atomic<float> m_masterGain { 1.0f }, m_masterBalance { 0.0f }, m_metronomeGain { 1.0f };
        std::atomic<float> m_masterPeak { 0.0f }, m_metronomePeak { 0.0f };
        float m_currentMasterGain { 1.0f };  // audio thread only
    };

    void pushCommand (CommandType type, int value = 0, std::int64_t frame = 0);
    void startPlayingFrom (std::int64_t frame) noexcept;
    void takePublishedPlayer() noexcept;
    void handleCommands() noexcept;
    void startFadeOut (AfterFade action) noexcept;
    void finishFadeOut() noexcept;
    void handleSongEnd() noexcept;

    void processChunk (const float* const* inputs, int numInputs, float* const* outputs, int numOutputs,
                       int offset, int numFrames) noexcept;
    void renderSegment (MBoxSongPlayer& song, const float* const* inputs, int numInputs,
                        float* const* outputs, int numOutputs, int offset, int numFrames) noexcept;
    void renderClip (const MBoxSongPlayer& song, const MBoxSongPlayer::Track& track, int numFrames) noexcept;
    void mixIntoBus (MBoxSongPlayer::Track& track, int busIndex, bool soloActive, int numFrames) noexcept;
    void applyTransportGain (float* const* outputs, int numOutputs, int offset, int numFrames) noexcept;

    // Commands, message thread -> audio thread
    juce::AbstractFifo m_commandFifo { 64 };
    std::array<Command, 64> m_commands {};

    // Publishing
    std::unique_ptr<MBoxPlaylistPlayer> m_published;               // message thread
    std::vector<std::unique_ptr<MBoxPlaylistPlayer>> m_retired;    // message thread
    std::uint64_t m_nextGeneration { 1 };                          // message thread
    std::atomic<MBoxPlaylistPlayer*> m_pending { nullptr };
    std::atomic<std::uint64_t> m_acknowledgedGeneration { 0 };
    MBoxPlaylistPlayer* m_active { nullptr };                      // audio thread

    // Transport, audio thread only
    TransportState m_state { TransportState::Stopped };
    int m_songIndex { 0 };
    int m_pendingSongIndex { -1 };
    std::int64_t m_position { 0 };
    float m_gain { 0.0f };
    float m_targetGain { 0.0f };
    AfterFade m_afterFade { AfterFade::Nothing };
    std::int64_t m_playFromFrame { 0 };

    // Status, written by the audio thread
    std::atomic<int> m_reportedState { (int) TransportState::Stopped };
    std::atomic<int> m_reportedSongIndex { 0 };
    std::atomic<std::int64_t> m_reportedPosition { 0 };

    std::array<Bus, numberOfMixBus> m_buses;

    // Scratch buffers and sounds, allocated by prepare()
    double m_sampleRate { supportedSampleRate };
    int m_fadeFrames { 441 };
    juce::AudioBuffer<float> m_busBuffers;    // 2 channels per bus
    juce::AudioBuffer<float> m_trackBuffer;   // the current track's source
    juce::AudioBuffer<float> m_insertBuffer;  // the source after one bus's inserts
    juce::AudioBuffer<float> m_clickBuffer;
    std::unique_ptr<MBoxClickSounds> m_clickSounds;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MBoxRenderer)
};
