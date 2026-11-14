#include "MBoxClipStreamer.h"

#include <algorithm>
#include <cmath>
#include <cstring>

//==============================================================================
MBoxClipStreamer::MBoxClipStreamer (std::unique_ptr<juce::AudioFormatReader> reader, double startSeconds)
    : m_reader (std::move (reader)),
      m_startSeconds (std::max (0.0, startSeconds)),
      m_fileSampleRate (m_reader != nullptr && m_reader->sampleRate > 0.0 ? m_reader->sampleRate : 44100.0),
      m_fileLength (m_reader != nullptr ? m_reader->lengthInSamples : 0)
{
    jassert (m_reader != nullptr);
}

//==============================================================================
// AUDIO THREAD (consumer)
//==============================================================================
bool MBoxClipStreamer::needsSeek (std::int64_t songSample, double deviceSampleRate) const noexcept
{
    if (m_consumerGeneration == 0 || ! juce::exactlyEqual (deviceSampleRate, m_consumerSampleRate))
        return true;

    return std::abs (songSample - (m_clipStartSample + m_consumerClipPos)) > seekTolerance;
}

void MBoxClipStreamer::seek (std::int64_t songSample, double deviceSampleRate) noexcept
{
    m_consumerSampleRate  = deviceSampleRate;
    m_clipStartSample     = std::llround (m_startSeconds * deviceSampleRate);
    m_consumerClipPos     = songSample - m_clipStartSample;
    m_consumerSeekClipPos = m_consumerClipPos;
    m_consumerSynced      = false;

    // 0 means "never seeked", so it is skipped when the counter wraps
    if (++m_consumerGeneration == 0)
        m_consumerGeneration = 1;

    m_seekClipPos.store (m_consumerClipPos, std::memory_order_relaxed);
    m_seekSampleRate.store (deviceSampleRate, std::memory_order_relaxed);
    m_seekGeneration.store (m_consumerGeneration, std::memory_order_release);
}

void MBoxClipStreamer::addTo (float* const* outputs, int numOutputs, int startSample, int numSamples) noexcept
{
    if (numSamples <= 0 || m_consumerGeneration == 0)
        return;

    // The stream moves on whatever the ring holds, so the clip stays in time with the song
    const auto blockStart = m_consumerClipPos;
    m_consumerClipPos += numSamples;

    // The read-ahead thread has not refilled the ring since the last seek: the ring belongs to it
    if (m_readyGeneration.load (std::memory_order_acquire) != m_consumerGeneration)
        return;

    // First block since the ring became ready: the ring starts at the seek position
    if (! m_consumerSynced)
    {
        m_ringClipPos    = m_consumerSeekClipPos;
        m_consumerSynced = true;
    }

    // Drop what the output already moved past (the blocks played while the ring was refilling, or an underrun)
    if (m_ringClipPos < blockStart)
    {
        const auto toDrop = (int) std::min<std::int64_t> (blockStart - m_ringClipPos, m_fifo.getNumReady());

        int start1, size1, start2, size2;
        m_fifo.prepareToRead (toDrop, start1, size1, start2, size2);
        m_fifo.finishedRead (size1 + size2);
        m_ringClipPos += size1 + size2;

        if (m_ringClipPos < blockStart)
            return;
    }

    // The ring never runs ahead of the output, but stay safe if it does
    const auto gap    = (int) std::min<std::int64_t> (m_ringClipPos - blockStart, numSamples);
    const auto toRead = std::min (numSamples - gap, m_fifo.getNumReady());

    if (toRead <= 0)
        return;

    int start1, size1, start2, size2;
    m_fifo.prepareToRead (toRead, start1, size1, start2, size2);

    const auto numChannelsToMix = std::min (numChannels, numOutputs);

    const auto mixRegion = [&] (int ringStart, int count, int outputOffset)
    {
        if (count <= 0)
            return;

        for (int channel = 0; channel < numChannelsToMix; ++channel)
            if (outputs[channel] != nullptr)
                juce::FloatVectorOperations::add (outputs[channel] + startSample + gap + outputOffset,
                                                  m_ring.getReadPointer (channel, ringStart),
                                                  count);
    };

    mixRegion (start1, size1, 0);
    mixRegion (start2, size2, size1);

    m_fifo.finishedRead (size1 + size2);
    m_ringClipPos += size1 + size2;
}

//==============================================================================
// READ-AHEAD THREAD (producer)
//==============================================================================
int MBoxClipStreamer::useTimeSlice()
{
    constexpr int idleMs = 10;

    const auto generation = m_seekGeneration.load (std::memory_order_acquire);

    // Never seeked: nothing to stream yet
    if (generation == 0)
        return idleMs;

    // A new seek: refill the ring from the new position, then hand it to the audio thread
    if (generation != m_producerGeneration)
    {
        startGeneration (generation);
        produce (maxSamplesPerSlice);
        m_readyGeneration.store (generation, std::memory_order_release);
        return 0;
    }

    // Keep the ring topped up. A full slice means there may be more to do, so come back at once.
    return produce (maxSamplesPerSlice) == maxSamplesPerSlice ? 0 : idleMs;
}

void MBoxClipStreamer::startGeneration (std::uint32_t generation)
{
    // Read after the generation (acquire), so this is that generation's position or a newer one.
    // A newer one is harmless: the audio thread ignores this generation, and the next slice restarts.
    const auto clipPos    = m_seekClipPos.load (std::memory_order_relaxed);
    const auto sampleRate = m_seekSampleRate.load (std::memory_order_relaxed);

    m_producerGeneration = generation;

    // The audio thread does not read the ring while the ready generation is stale
    m_fifo.reset();

    m_ratio = sampleRate > 0.0 ? m_fileSampleRate / sampleRate : 1.0;

    if (std::abs (m_ratio - 1.0) < 1.0e-9)
        m_ratio = 1.0;

    m_clipLength      = (std::int64_t) std::ceil ((double) m_fileLength / m_ratio);
    m_producerClipPos = clipPos;
    m_fileReadPos     = clipPos > 0 ? (std::int64_t) std::floor ((double) clipPos * m_ratio) : 0;
    m_inputCount      = 0;

    for (auto& interpolator : m_interpolators)
        interpolator.reset();

    if (m_ratio != 1.0)
        m_input.setSize (numChannels, (int) std::ceil (chunkSize * m_ratio) + 2 * interpolatorPadding, false, false, true);
}

int MBoxClipStreamer::produce (int maxSamples)
{
    int written = 0;

    while (written < maxSamples)
    {
        // A newer seek makes the rest of this work useless
        if (m_seekGeneration.load (std::memory_order_relaxed) != m_producerGeneration)
            break;

        auto numSamples = std::min ({ chunkSize, maxSamples - written, m_fifo.getFreeSpace() });

        // Before the clip start, write silence up to the start; from there on, read the file up to its end.
        // So each write is either all silence or all file.
        if (m_producerClipPos < 0)
            numSamples = (int) std::min<std::int64_t> (numSamples, -m_producerClipPos);
        else
            numSamples = (int) std::min<std::int64_t> (numSamples, m_clipLength - m_producerClipPos);

        if (numSamples <= 0)
            break;

        int start1, size1, start2, size2;
        m_fifo.prepareToWrite (numSamples, start1, size1, start2, size2);

        for (const auto [start, size] : { std::pair { start1, size1 }, std::pair { start2, size2 } })
        {
            if (size <= 0)
                continue;

            float* dest[numChannels] = { m_ring.getWritePointer (0, start), m_ring.getWritePointer (1, start) };

            if (m_producerClipPos < 0)
                for (auto* channel : dest)
                    juce::FloatVectorOperations::clear (channel, size);
            else
                readFromFile (dest, size);

            m_producerClipPos += size;
        }

        m_fifo.finishedWrite (size1 + size2);
        written += size1 + size2;
    }

    return written;
}

void MBoxClipStreamer::readFromFile (float* const* dest, int numSamples)
{
    // Same rate: the clip position is the file position. The reader dupes a mono file to both
    // channels and only reads the first two channels of a wider one.
    if (m_ratio == 1.0)
    {
        juce::AudioBuffer<float> view (dest, numChannels, numSamples);
        m_reader->read (&view, 0, numSamples, m_producerClipPos, true, true);
        return;
    }

    // Different rate: top up the resampler's input from the file, then interpolate. The reader
    // returns zeros past the end of the file, which also flushes the interpolator at the end.
    const auto needed = (int) std::ceil (numSamples * m_ratio) + interpolatorPadding;

    if (m_inputCount < needed)
    {
        const auto toRead = needed - m_inputCount;
        float* inputDest[numChannels] = { m_input.getWritePointer (0, m_inputCount), m_input.getWritePointer (1, m_inputCount) };
        juce::AudioBuffer<float> view (inputDest, numChannels, toRead);

        m_reader->read (&view, 0, toRead, m_fileReadPos, true, true);
        m_fileReadPos += toRead;
        m_inputCount  += toRead;
    }

    // Both channels have the same ratio and state, so they use the same number of input samples
    int used = 0;

    for (int channel = 0; channel < numChannels; ++channel)
        used = m_interpolators[(size_t) channel].process (m_ratio, m_input.getReadPointer (channel), dest[channel], numSamples);

    // Keep the input the interpolator has not used yet for the next call
    const auto remaining = std::max (0, m_inputCount - used);

    for (int channel = 0; channel < numChannels; ++channel)
    {
        auto* data = m_input.getWritePointer (channel);
        std::memmove (data, data + used, (size_t) remaining * sizeof (float));
    }

    m_inputCount = remaining;
}
