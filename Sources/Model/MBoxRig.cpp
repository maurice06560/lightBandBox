#include "MBoxRig.h"

#include <juce_audio_basics/juce_audio_basics.h>

//==============================================================================
// MBoxMixBus
//==============================================================================
MBoxMixBus::MBoxMixBus (const int id)
    : m_busId (id),
      m_name ("MixBus " + juce::String (id)),
      m_outputs { (id - 1) * 2, (id - 1) * 2 + 1 }  // bus 1 on outputs 1-2, bus 2 on 3-4...
{
}

bool MBoxMixBus::rename (const juce::String& name)
{
    if (name.trim().isEmpty())
        return false;

    m_name = name.trim();
    return true;
}

juce::String MBoxMixBus::getName() const  { return m_name; }

bool MBoxMixBus::setOutputs (const std::pair<int, int>& outputs)
{
    if (outputs.first < -1 || outputs.second < -1 || (outputs.first == -1 && outputs.second != -1))
        return false;

    m_outputs = outputs;
    return true;
}

std::pair<int, int> MBoxMixBus::getOutputs() const  { return m_outputs; }

void MBoxMixBus::setMasterVolume (double volumeDb) noexcept     { m_masterVolume = juce::jmin (maxVolumeDb, volumeDb); }
void MBoxMixBus::setMasterBalance (double balance) noexcept     { m_masterBalance = juce::jlimit (-1.0, 1.0, balance); }
void MBoxMixBus::setMetronomeVolume (double volumeDb) noexcept  { m_metronome.m_volume = juce::jmin (maxVolumeDb, volumeDb); }

//==============================================================================
// MBoxRig
//==============================================================================
MBoxRig::MBoxRig()
    : m_mixBuses { MBoxMixBus (1), MBoxMixBus (2), MBoxMixBus (3) }
{
    static_assert (numberOfMixBus == 3, "Update the m_mixBuses initialiser");
}

float volumeDbToGain (double volumeDb) noexcept
{
    return volumeDb <= minVolumeDb ? 0.0f
                                   : juce::Decibels::decibelsToGain ((float) juce::jmin (volumeDb, maxVolumeDb));
}
