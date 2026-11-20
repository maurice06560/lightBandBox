#include "MBoxPluginHost.h"

//==============================================================================
MBoxPluginHost::MBoxPluginHost (const juce::File& pluginListFile)
    : m_pluginListFile (pluginListFile)
{
    juce::addHeadlessDefaultFormatsToManager (m_formats);
}

juce::Result MBoxPluginHost::loadPluginList()
{
    m_knownPlugins.clear();

    if (! m_pluginListFile.existsAsFile())
        return juce::Result::ok();

    if (auto xml = juce::parseXML (m_pluginListFile))
    {
        m_knownPlugins.recreateFromXml (*xml);
        return juce::Result::ok();
    }

    return juce::Result::fail ("Can't read " + m_pluginListFile.getFullPathName());
}

juce::Result MBoxPluginHost::scanForPlugins()
{
    JUCE_ASSERT_MESSAGE_THREAD

    for (auto* format : m_formats.getFormats())
    {
        juce::PluginDirectoryScanner scanner (m_knownPlugins, *format, format->getDefaultLocationsToSearch(),
                                              true, m_pluginListFile.getSiblingFile ("plugins-dead-mans-pedal.txt"));
        juce::String pluginName;

        while (scanner.scanNextFile (true, pluginName))
            juce::Logger::writeToLog ("MBoxPluginHost: scanned " + pluginName);
    }

    if (auto xml = m_knownPlugins.createXml(); xml == nullptr || ! xml->writeTo (m_pluginListFile))
        return juce::Result::fail ("Can't write " + m_pluginListFile.getFullPathName());

    return juce::Result::ok();
}

juce::Array<juce::PluginDescription> MBoxPluginHost::getKnownPlugins() const
{
    return m_knownPlugins.getTypes();
}

//==============================================================================
// Matching: the exact identity first, then name and manufacturer only (the unique id can
// change between plugin versions), then the stored identifier as is
//==============================================================================
juce::PluginDescription MBoxPluginHost::findDescription (const MBoxEffect& effect) const
{
    const auto types = m_knownPlugins.getTypes();

    for (const auto& type : types)
        if (type.pluginFormatName == effect.m_format && type.name == effect.m_name
            && type.manufacturerName == effect.m_manufacturer && type.uniqueId == effect.m_uniqueId)
            return type;

    for (const auto& type : types)
        if (type.pluginFormatName == effect.m_format && type.name == effect.m_name
            && type.manufacturerName == effect.m_manufacturer)
            return type;

    return effect.toDescription();
}

std::unique_ptr<juce::AudioPluginInstance> MBoxPluginHost::createInstance (const MBoxEffect& effect, double sampleRate,
                                                                          int blockSize, juce::String& error)
{
    JUCE_ASSERT_MESSAGE_THREAD

    auto instance = m_formats.createPluginInstance (findDescription (effect), sampleRate, blockSize, error);

    if (instance == nullptr)
    {
        error = "Can't load plugin " + effect.m_name + " (" + effect.m_format + "): " + error;
        return nullptr;
    }

    if (effect.m_state.getSize() > 0)
        instance->setStateInformation (effect.m_state.getData(), (int) effect.m_state.getSize());

    return instance;
}

std::unique_ptr<MBoxEffectChain> MBoxPluginHost::createChain (const std::vector<MBoxEffect>& effects, int numChannels,
                                                              double sampleRate, int blockSize, juce::String& error)
{
    auto chain = std::make_unique<MBoxEffectChain> (numChannels);

    for (const auto& effect : effects)
    {
        auto instance = createInstance (effect, sampleRate, blockSize, error);

        if (instance == nullptr)
            return nullptr;

        chain->addPlugin (std::move (instance), effect.m_bypassed);
    }

    return chain;
}

std::unique_ptr<MBoxEffectChain> MBoxPluginHost::createChainFromLoaderThread (const std::vector<MBoxEffect>& effects, int numChannels,
                                                                              double sampleRate, int blockSize, juce::String& error)
{
    jassert (! juce::MessageManager::getInstance()->isThisTheMessageThread());

    // Chains without plugins need nothing from the message thread
    if (effects.empty())
        return std::make_unique<MBoxEffectChain> (numChannels);

    // Shared with the message-thread job, which may still run after a timeout here
    struct Job
    {
        std::unique_ptr<MBoxEffectChain> m_chain;
        juce::String m_error;
        juce::WaitableEvent m_done;
    };

    auto job = std::make_shared<Job>();

    juce::MessageManager::callAsync ([this, job, effects, numChannels, sampleRate, blockSize]
    {
        job->m_chain = createChain (effects, numChannels, sampleRate, blockSize, job->m_error);
        job->m_done.signal();
    });

    if (! job->m_done.wait (60000))
    {
        error = "Timed out waiting for the message thread to create plugins";
        return nullptr;
    }

    error = job->m_error;
    return std::move (job->m_chain);
}
