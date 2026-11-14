#include "MBoxClipStreamer.h"

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
int MBoxClipStreamer::useTimeSlice()
{
    constexpr int idleMs = 10;

    return idleMs;
}