#pragma once

#include "MBoxTrack.h"
#include "MBoxCommonStructures.h"

#include <juce_core/juce_core.h>

class MBoxAudioTrack final : public MBoxTrack
{
public:
    MBoxAudioTrack() = default;
    MBoxAudioTrack (const Bar& startAudio, const juce::String& fileName)
        : m_startAudio (startAudio), m_fileName (fileName) {}
    ~MBoxAudioTrack() override = default;

    // Getters
    const Bar& getStartAudio() const { return m_startAudio; }
    const juce::String& getFileName() const { return m_fileName; }

private:
    Bar m_startAudio { 1, 1, 1, 0 };    // Position in the song where the audio file starts
    juce::String m_fileName;
};
