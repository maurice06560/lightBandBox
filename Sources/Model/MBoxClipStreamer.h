#pragma once

#include <juce_audio_formats/juce_audio_formats.h>

#include <array>
#include <atomic>
#include <cstdint>
#include <memory>

//==============================================================================
/**
    Streams the audio file of one audio track from disk, so the audio thread can mix
    it without touching the disk.

    The streamer is a TimeSliceClient: on a TimeSliceThread it reads the file ahead of
    the playhead into a lock-free ring buffer, resampling to the device rate when the
    file has a different one. The audio thread takes the samples out of the ring with
    addTo(). The file always comes out as two channels: a mono file goes to both, and
    a file with more than two channels only uses its first two.

    Positions are in device samples. The clip starts at startSeconds of song time; a
    "song sample" is a position in samples from the song's tick 0, and a "clip position"
    is a position in samples from the start of the file.

    Seeking: the audio thread calls seek() whenever the song position jumps. It publishes
    the new clip position under a new generation number. The read-ahead thread sees the
    new generation, empties the ring, refills it from the new position and then publishes
    that generation as ready. Until then the audio thread mixes nothing for this clip, so
    neither thread ever waits for the other.

    Threads: construction and destruction on the message thread, while the streamer is not
    registered with the TimeSliceThread. seek(), needsSeek() and addTo() on the audio thread
    only. useTimeSlice() on the TimeSliceThread only.
*/
class MBoxClipStreamer final : public juce::TimeSliceClient
{
public:
    //==============================================================================
    /** Takes ownership of the reader. startSeconds is where the clip starts in song time. */
    MBoxClipStreamer (std::unique_ptr<juce::AudioFormatReader> reader, double startSeconds);
    ~MBoxClipStreamer() override = default;

    //==============================================================================
    // AUDIO THREAD (consumer)
    //==============================================================================

    /** Returns true if the stream is not where songSample says it should be: it was never seeked,
        the device rate changed, or the song position jumped since the last addTo(). */
    bool needsSeek (std::int64_t songSample, double deviceSampleRate) const noexcept;

    /** Moves the stream to songSample. The clip is silent until the read-ahead thread has refilled the ring. */
    void seek (std::int64_t songSample, double deviceSampleRate) noexcept;

    /** Adds the next numSamples of the clip to outputs[0] and outputs[1], from startSample.
        The stream always advances by numSamples, even when the ring has not caught up yet. */
    void addTo (float* const* outputs, int numOutputs, int startSample, int numSamples) noexcept;

    //==============================================================================
    // READ-AHEAD THREAD (producer)
    //==============================================================================

    int useTimeSlice() override;

private:
    //==============================================================================
    static constexpr int numChannels = 2;
    static constexpr int ringSize = 1 << 16;            // ~1.5 s at 44.1 kHz.
    static constexpr int chunkSize = 4096;              // Samples produced per disk read.
    static constexpr int maxSamplesPerSlice = 4 * chunkSize;
    static constexpr int interpolatorPadding = 8;       // Extra input samples read for the resampler.
    static constexpr std::int64_t seekTolerance = 2;    // Rounding drift (in samples) that is not a jump.

    //==============================================================================
    /** Resets the producer to the clip position published under the given generation. */
    void startGeneration (std::uint32_t generation);

    /** Writes up to maxSamples into the ring. Returns the number written, 0 when the ring is full or the clip has ended. */
    int produce (int maxSamples);

    /** Fills numSamples device samples of the ring from the file, from m_producerClipPos. */
    void readFromFile (float* const* dest, int numSamples);

    //==============================================================================
    // Fixed at construction.
    const std::unique_ptr<juce::AudioFormatReader> m_reader;
    const double m_startSeconds;
    const double m_fileSampleRate;
    const std::int64_t m_fileLength;

    // Seek requests, written by the audio thread. The position and rate are stored before the
    // generation (release), so a producer that sees a generation sees its position or a newer one.
    std::atomic<std::uint32_t> m_seekGeneration { 0 };
    std::atomic<std::int64_t> m_seekClipPos { 0 };
    std::atomic<double> m_seekSampleRate { 44100.0 };

    // The generation whose samples are in the ring, written by the read-ahead thread (release) once
    // the ring holds samples from that generation's position.
    std::atomic<std::uint32_t> m_readyGeneration { 0 };

    // Shared by both threads. The audio thread reads and the read-ahead thread writes, through
    // the FIFO. The read-ahead thread is the only one that resets the FIFO, and only when the
    // audio thread is not reading from it (the ready generation is stale).
    juce::AbstractFifo m_fifo { ringSize };
    juce::AudioBuffer<float> m_ring { numChannels, ringSize };

    // Audio thread only.
    std::uint32_t m_consumerGeneration { 0 };
    double m_consumerSampleRate { 0.0 };
    std::int64_t m_clipStartSample { 0 };               // Song sample of clip position 0 at the current rate.
    std::int64_t m_consumerClipPos { 0 };               // Clip position of the next sample to output.
    std::int64_t m_consumerSeekClipPos { 0 };           // Clip position of the last seek.
    std::int64_t m_ringClipPos { 0 };                   // Clip position of the next sample in the ring.
    bool m_consumerSynced { false };                    // True once the ring of the current generation was seen.

    // Read-ahead thread only.
    std::uint32_t m_producerGeneration { 0 };
    std::int64_t m_producerClipPos { 0 };               // Clip position of the next sample to write.
    std::int64_t m_clipLength { 0 };                    // Clip length in device samples.
    std::int64_t m_fileReadPos { 0 };                   // Next file sample to read (resampling only).
    double m_ratio { 1.0 };                             // File samples per device sample.
    juce::AudioBuffer<float> m_input;                   // File samples waiting for the resampler.
    int m_inputCount { 0 };
    std::array<juce::LagrangeInterpolator, numChannels> m_interpolators;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MBoxClipStreamer)
};
