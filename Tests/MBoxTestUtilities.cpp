#include "MBoxTestUtilities.h"

namespace MBoxTest
{
    TemporaryDirectory::TemporaryDirectory()
        : m_directory (juce::File::getSpecialLocation (juce::File::tempDirectory)
                           .getNonexistentChildFile ("MBoxTest", {}, false))
    {
        m_directory.createDirectory();
    }

    TemporaryDirectory::~TemporaryDirectory()
    {
        m_directory.deleteRecursively();
    }

    juce::Result writeWav (const juce::File& file, std::int64_t numFrames,
                           const std::function<std::pair<float, float> (std::int64_t)>& generator,
                           double sampleRate, int bitsPerSample, int numChannels)
    {
        file.getParentDirectory().createDirectory();
        file.deleteFile();

        std::unique_ptr<juce::OutputStream> stream = file.createOutputStream();
        juce::WavAudioFormat format;
        auto writer = format.createWriterFor (stream, juce::AudioFormatWriterOptions {}
                                                          .withSampleRate (sampleRate)
                                                          .withNumChannels (numChannels)
                                                          .withBitsPerSample (bitsPerSample));

        if (writer == nullptr)
            return juce::Result::fail ("Can't create a WAV writer");

        juce::AudioBuffer<float> buffer (numChannels, (int) numFrames);

        for (int i = 0; i < (int) numFrames; ++i)
        {
            const auto [left, right] = generator (i);
            buffer.setSample (0, i, left);

            if (numChannels > 1)
                buffer.setSample (1, i, right);
        }

        return writer->writeFromAudioSampleBuffer (buffer, 0, (int) numFrames) ? juce::Result::ok()
                                                                              : juce::Result::fail ("Write failed");
    }

    juce::AudioBuffer<float> render (MBoxRenderer& renderer, int numFrames, int numOutputs, float inputValue,
                                     int numInputs, int blockSize)
    {
        juce::AudioBuffer<float> outputs (numOutputs, numFrames);
        juce::AudioBuffer<float> inputs (numInputs, blockSize);
        std::vector<const float*> inputPointers ((size_t) numInputs);
        std::vector<float*> outputPointers ((size_t) numOutputs);

        for (int start = 0; start < numFrames; start += blockSize)
        {
            const auto length = juce::jmin (blockSize, numFrames - start);

            for (int ch = 0; ch < numInputs; ++ch)
            {
                juce::FloatVectorOperations::fill (inputs.getWritePointer (ch), inputValue, length);
                inputPointers[(size_t) ch] = inputs.getReadPointer (ch);
            }

            for (int ch = 0; ch < numOutputs; ++ch)
                outputPointers[(size_t) ch] = outputs.getWritePointer (ch, start);

            renderer.process (inputPointers.data(), numInputs, outputPointers.data(), numOutputs, length);
        }

        return outputs;
    }

    int findOnset (const juce::AudioBuffer<float>& buffer, int channel, int start, float threshold)
    {
        const auto* samples = buffer.getReadPointer (channel);

        for (int i = juce::jmax (0, start); i < buffer.getNumSamples(); ++i)
            if (std::abs (samples[i]) > threshold)
                return i;

        return -1;
    }

    bool waitUntil (const std::function<bool()>& condition, int timeoutMs)
    {
        const auto end = juce::Time::getMillisecondCounter() + (juce::uint32) timeoutMs;

        while (! condition())
        {
            if (juce::Time::getMillisecondCounter() > end)
                return false;

            juce::MessageManager::getInstance()->runDispatchLoopUntil (10);
        }

        return true;
    }
}
