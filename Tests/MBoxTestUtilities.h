#pragma once

#include "Audio/MBoxRenderer.h"

#include <juce_audio_formats/juce_audio_formats.h>

//==============================================================================
// Helpers shared by the unit tests
//==============================================================================
namespace MBoxTest
{
    // A new empty folder in the system temp folder, deleted with the object
    struct TemporaryDirectory
    {
        TemporaryDirectory();
        ~TemporaryDirectory();

        juce::File m_directory;
    };

    // Writes a WAV whose frames come from generator (left, right). Defaults to the supported format.
    juce::Result writeWav (const juce::File& file, std::int64_t numFrames,
                           const std::function<std::pair<float, float> (std::int64_t)>& generator,
                           double sampleRate = 44100.0, int bitsPerSample = 16, int numChannels = 2);

    // Runs the renderer for numFrames in blocks of blockSize, with every input channel set to
    // inputValue, and returns the outputs
    juce::AudioBuffer<float> render (MBoxRenderer& renderer, int numFrames, int numOutputs = 8,
                                     float inputValue = 0.0f, int numInputs = 2, int blockSize = 128);

    // Index of the first sample whose magnitude exceeds threshold, from start, or -1
    int findOnset (const juce::AudioBuffer<float>& buffer, int channel, int start = 0, float threshold = 1.0e-4f);

    // Runs the message loop until the condition is true or the timeout expires. Returns the condition.
    bool waitUntil (const std::function<bool()>& condition, int timeoutMs = 10000);
}
