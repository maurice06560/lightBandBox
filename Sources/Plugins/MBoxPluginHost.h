#pragma once

#include "MBoxEffectChain.h"
#include "../Model/MBoxBank.h"

//==============================================================================
// Finds and creates the plugins used by effect chains.
//
// The list of installed plugins is built by scanning (done by the UI app, since scanning is
// slow and a broken plugin can crash the process) and saved to plugins.xml in the data folder.
// The rack app only reads that list.
//
// An MBoxEffect is matched against the list by format, name, manufacturer and unique id, so a
// data folder works on any computer where the same plugins are installed, wherever they are.
//
// Plugins are created on the message thread, which most VST3 and AU plugins expect. Loader
// threads use createChainFromLoaderThread(), which runs the creation there and waits.
//==============================================================================
class MBoxPluginHost
{
public:
    explicit MBoxPluginHost (const juce::File& pluginListFile);

    // Reads plugins.xml. A missing file gives an empty list.
    juce::Result loadPluginList();

    // Scans the default plugin folders of every supported format and saves the list.
    // Slow (seconds to minutes); call from the UI, on the message thread.
    juce::Result scanForPlugins();

    juce::Array<juce::PluginDescription> getKnownPlugins() const;

    // Finds the installed plugin matching an effect. Falls back to the effect's own
    // identifier when the list has no match.
    juce::PluginDescription findDescription (const MBoxEffect& effect) const;

    // Message thread. Creates a plugin and restores its saved state.
    std::unique_ptr<juce::AudioPluginInstance> createInstance (const MBoxEffect& effect, double sampleRate,
                                                              int blockSize, juce::String& error);

    // Message thread. Creates every plugin of a chain (not yet prepared). Fails on the first
    // plugin that can't be created.
    std::unique_ptr<MBoxEffectChain> createChain (const std::vector<MBoxEffect>& effects, int numChannels,
                                                  double sampleRate, int blockSize, juce::String& error);

    // Any thread except the message thread: runs createChain on the message thread and waits
    // for it. Returns nullptr with an error if the message thread doesn't answer in time.
    std::unique_ptr<MBoxEffectChain> createChainFromLoaderThread (const std::vector<MBoxEffect>& effects, int numChannels,
                                                                  double sampleRate, int blockSize, juce::String& error);

private:
    juce::File m_pluginListFile;
    juce::AudioPluginFormatManager m_formats;
    juce::KnownPluginList m_knownPlugins;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MBoxPluginHost)
};
