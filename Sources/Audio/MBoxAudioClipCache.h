#pragma once

#include <juce_audio_formats/juce_audio_formats.h>

#include <map>
#include <memory>
#include <vector>

//==============================================================================
// Reads a WAV header and checks it's a supported file: 44100 Hz, 16-bit integer PCM, stereo.
// On success, sets lengthInFrames.
//==============================================================================
juce::Result readSupportedWavLength (const juce::File& file, std::int64_t& lengthInFrames);

//==============================================================================
// A decoded WAV file, kept in memory as interleaved 16-bit stereo (left, right, left...),
// which halves the memory use compared to floats. Immutable once loaded.
//==============================================================================
struct MBoxAudioClip
{
    juce::File m_file;
    std::vector<std::int16_t> m_samples;
    std::int64_t m_numFrames { 0 };

    std::size_t getSizeInBytes() const noexcept  { return m_samples.size() * sizeof (std::int16_t); }
};

//==============================================================================
// Decodes each WAV file once and shares it between every song using it.
// getOrLoad() can be called from several loader threads at once. Clips are shared_ptrs: a
// clip stays alive as long as a song player uses it, even after the cache drops it.
//==============================================================================
class MBoxAudioClipCache
{
public:
    MBoxAudioClipCache();

    // Returns the decoded file, decoding it on first use (on the calling thread). Returns
    // nullptr and sets error when the file can't be read or isn't a supported WAV.
    // A cancelled load (shouldCancel returning true) also returns nullptr.
    std::shared_ptr<const MBoxAudioClip> getOrLoad (const juce::File& file, juce::String& error,
                                                     const std::function<bool()>& shouldCancel = {});

    // Drops every clip not in the given list, so a new playlist doesn't keep the old one's audio
    void retainOnly (const juce::Array<juce::File>& files);

    std::size_t getTotalSizeInBytes() const;

private:
    std::shared_ptr<const MBoxAudioClip> decode (const juce::File& file, juce::String& error,
                                                 const std::function<bool()>& shouldCancel);

    mutable juce::CriticalSection m_lock;
    std::map<juce::String, std::shared_ptr<const MBoxAudioClip>> m_clips;  // keyed by full path

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MBoxAudioClipCache)
};
