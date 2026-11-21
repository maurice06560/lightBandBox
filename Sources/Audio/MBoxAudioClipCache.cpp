#include "MBoxAudioClipCache.h"
#include "../Model/MBoxTypes.h"

namespace
{
    // Opens a WAV reader and checks the format. Returns nullptr and sets error on failure.
    std::unique_ptr<juce::AudioFormatReader> createSupportedWavReader (const juce::File& file, juce::String& error)
    {
        if (! file.existsAsFile())
        {
            error = "File not found: " + file.getFullPathName();
            return nullptr;
        }

        juce::WavAudioFormat wavFormat;
        std::unique_ptr<juce::AudioFormatReader> reader (wavFormat.createReaderFor (file.createInputStream().release(), true));

        if (reader == nullptr)
        {
            error = "Not a WAV file: " + file.getFullPathName();
            return nullptr;
        }

        if (! juce::exactlyEqual (reader->sampleRate, supportedSampleRate) || reader->bitsPerSample != 16
            || reader->numChannels != 2 || reader->usesFloatingPointData)
        {
            error = "Unsupported WAV format (" + juce::String (reader->sampleRate, 0) + " Hz, "
                  + juce::String (reader->bitsPerSample) + "-bit, " + juce::String (reader->numChannels)
                  + " channels): only 44100 Hz, 16-bit stereo files are supported: " + file.getFullPathName();
            return nullptr;
        }

        return reader;
    }
}

//==============================================================================
juce::Result readSupportedWavLength (const juce::File& file, std::int64_t& lengthInFrames)
{
    juce::String error;
    auto reader = createSupportedWavReader (file, error);

    if (reader == nullptr)
        return juce::Result::fail (error);

    lengthInFrames = reader->lengthInSamples;
    return juce::Result::ok();
}

//==============================================================================
MBoxAudioClipCache::MBoxAudioClipCache() = default;

std::shared_ptr<const MBoxAudioClip> MBoxAudioClipCache::getOrLoad (const juce::File& file, juce::String& error,
                                                                     const std::function<bool()>& shouldCancel)
{
    const auto key = file.getFullPathName();

    {
        const juce::ScopedLock lock (m_lock);

        if (auto it = m_clips.find (key); it != m_clips.end())
            return it->second;
    }

    // Decoded outside the lock so several files load in parallel. Two threads asking for the
    // same new file both decode it; the first one stored wins.
    auto clip = decode (file, error, shouldCancel);

    if (clip == nullptr)
        return nullptr;

    const juce::ScopedLock lock (m_lock);
    return m_clips.emplace (key, std::move (clip)).first->second;
}

//==============================================================================
// Reads the file in blocks. JUCE returns integer samples left-justified in 32 bits, so a
// 16-bit sample is the top 16 bits.
//==============================================================================
std::shared_ptr<const MBoxAudioClip> MBoxAudioClipCache::decode (const juce::File& file, juce::String& error,
                                                                 const std::function<bool()>& shouldCancel)
{
    auto reader = createSupportedWavReader (file, error);

    if (reader == nullptr)
        return nullptr;

    auto clip = std::make_shared<MBoxAudioClip>();
    clip->m_file = file;
    clip->m_numFrames = reader->lengthInSamples;
    clip->m_samples.resize ((size_t) clip->m_numFrames * 2);

    constexpr int blockSize = 65536;
    juce::HeapBlock<int> left (blockSize), right (blockSize);
    int* channels[] = { left.get(), right.get() };

    for (std::int64_t start = 0; start < clip->m_numFrames; start += blockSize)
    {
        if (shouldCancel && shouldCancel())
            return nullptr;

        const auto numFrames = (int) juce::jmin ((std::int64_t) blockSize, clip->m_numFrames - start);

        if (! reader->read (channels, 2, start, numFrames, false))
        {
            error = "Failed to read " + file.getFullPathName();
            return nullptr;
        }

        auto* destination = clip->m_samples.data() + start * 2;

        for (int i = 0; i < numFrames; ++i)
        {
            destination[i * 2]     = (std::int16_t) (left[i] >> 16);
            destination[i * 2 + 1] = (std::int16_t) (right[i] >> 16);
        }
    }

    return clip;
}

void MBoxAudioClipCache::retainOnly (const juce::Array<juce::File>& files)
{
    const juce::ScopedLock lock (m_lock);

    for (auto it = m_clips.begin(); it != m_clips.end();)
    {
        if (files.contains (juce::File (it->first)))
            ++it;
        else
            it = m_clips.erase (it);
    }
}

std::size_t MBoxAudioClipCache::getTotalSizeInBytes() const
{
    const juce::ScopedLock lock (m_lock);
    std::size_t total = 0;

    for (const auto& [path, clip] : m_clips)
        total += clip->getSizeInBytes();

    return total;
}
