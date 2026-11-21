// TEMPORARY test code, behind the "Test" button in the transport bar. Remove it with the button.

#include "PluginTest.h"

#include "../Effects/MAudioEffect.h"
#include "../MBoxApplication.h"

#include <iostream>

namespace
{
    constexpr int bankIndex = 1;   // Bank 2
    constexpr int slotIndex = 0;   // slot 1: Metallica

    /** The parameter named exactly "HOT Amp Gain" (not the input gain), or nullptr. */
    juce::AudioProcessorParameter* findHotAmpGain (const juce::Array<juce::AudioProcessorParameter*>& parameters)
    {
        for (auto* parameter : parameters)
            if (parameter->getName (100) == "HOT Amp Gain")
                return parameter;

        return nullptr;
    }

    juce::File presetFile()
    {
        return juce::File::getSpecialLocation (juce::File::userHomeDirectory)
            .getChildFile ("Library/Audio/Presets/Neural DSP/Archetype Gojira X/User/Metallica Copy.preset");
    }
}

void PluginTest::run (Session& session)
{
    auto& slot = session.getApp().getBanks()[bankIndex].m_bankSlots[slotIndex];

    if (slot.m_effectChain.empty())
    {
        std::cout << "PluginTest: bank 2, slot 1 has no effect" << std::endl;
        return;
    }

    auto* plugin = dynamic_cast<MAudioEffect*> (&slot.m_effectChain.front().getEffect());

    if (plugin == nullptr)
    {
        std::cout << "PluginTest: the first effect of bank 2, slot 1 is not a plugin" << std::endl;
        return;
    }

    auto* instance = plugin->getInstance();

    if (instance == nullptr)
    {
        std::cout << "PluginTest: " << plugin->getName() << " is missing: " << plugin->getLoadError() << std::endl;
        return;
    }

    std::cout << "PluginTest: " << plugin->getName() << " (" << plugin->getDescription().pluginFormatName << ")" << std::endl;

    // 1) Preset name and gain.
    const auto program = instance->getCurrentProgram();
    std::cout << "  preset: " << instance->getProgramName (program) << " (program " << program << " of " << instance->getNumPrograms() << ")" << std::endl;

    const auto& parameters = instance->getParameters();

    for (auto* parameter : parameters)
        std::cout << "  parameter: " << parameter->getName (100) << " = " << parameter->getValue()
                  << " (" << parameter->getCurrentValueAsText() << ")" << std::endl;

    auto* gain = findHotAmpGain (parameters);

    if (gain == nullptr)
    {
        std::cout << "  HOT Amp Gain: no parameter with that name, nothing changed" << std::endl;
        return;
    }

    // 2) Halve the gain. An AudioParameterFloat holds the natural value; for anything else the normalised
    // value (0..1) is halved, which is the same thing for a range that starts at 0 and is linear.
    if (auto* floatParameter = dynamic_cast<juce::AudioParameterFloat*> (gain))
    {
        const auto current = floatParameter->get();
        *floatParameter = 0.5f * current;
        std::cout << "  HOT Amp Gain: " << current << " -> " << floatParameter->get() << std::endl;
    }
    else
    {
        const auto current = gain->getValue();
        gain->setValueNotifyingHost (0.5f * current);
        std::cout << "  HOT Amp Gain (normalised): " << current << " -> " << gain->getValue() << std::endl;
    }

    session.markBanksChanged();

    // 3) Save the preset. The plugin's own preset format isn't public, so this is JUCE's state for the
    // current program, not a file Neural DSP's preset browser reads.
    juce::MemoryBlock state;
    instance->getCurrentProgramStateInformation (state);

    const auto file = presetFile();
    const auto result = file.getParentDirectory().createDirectory();

    if (result.failed() || ! file.replaceWithData (state.getData(), state.getSize()))
    {
        std::cout << "  save: failed to write " << file.getFullPathName() << std::endl;
        return;
    }

    std::cout << "  saved: " << file.getFullPathName() << " (" << (int) state.getSize() << " bytes)" << std::endl;
}
