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
    // READ-AHEAD THREAD
    //==============================================================================

    int useTimeSlice() override;

private:
    //==============================================================================
    static constexpr int numChannels = 2;
    static constexpr int ringSize = 1 << 16;            // ~1.5 s at 44.1 kHz.
    static constexpr int chunkSize = 4096;              // Samples produced per disk read.
    static constexpr int maxSamplesPerSlice = 4 * chunkSize;
    static constexpr int interpolatorPadding = 8;       // Extra input samples read for the resampler.

    //==============================================================================
    // Fixed at construction.
    const std::unique_ptr<juce::AudioFormatReader> m_reader;
    const double m_startSeconds;
    const double m_fileSampleRate;
    const std::int64_t m_fileLength;

    // Shared by both threads. The audio thread reads and the read-ahead thread writes, through
    // the FIFO. The read-ahead thread is the only one that resets the FIFO, and only when the
    // audio thread is not reading from it (the ready generation is stale).
    juce::AbstractFifo m_fifo { ringSize };
    juce::AudioBuffer<float> m_ring { numChannels, ringSize };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MBoxClipStreamer)
};
