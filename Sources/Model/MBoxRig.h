#pragma once

#include "MBoxBank.h"

#include <juce_core/juce_core.h>

#include <array>
#include <utility>

//==============================================================================
// The metronome in one mix bus: its level in dB, and whether it plays there at all. Whether
// it clicks is set per song (MetronomeState); a bus with m_on false never hears it.
//==============================================================================
struct MBoxMetronome
{
    double m_volume { 0.0 };
    bool m_on { true };
};

//==============================================================================
// The rack-wide part of a mix bus: where it goes and its master section. The per-song part
// (each track's volume, balance, mute, solo and inserts) is in MBoxSong.
// m_outputs holds 0-based output channels. The bus is mono when the second one is -1, and
// silent when the first one is -1.
//==============================================================================
class MBoxMixBus
{
public:
    explicit MBoxMixBus (const int id = 1);

    int getId() const noexcept  { return m_busId; }

    bool rename (const juce::String& name);
    juce::String getName() const;

    // Fails for channels below -1, or a second channel without a first one
    bool setOutputs (const std::pair<int, int>& outputs);
    std::pair<int, int> getOutputs() const;

    void setMasterVolume (double volumeDb) noexcept;
    double getMasterVolume() const noexcept  { return m_masterVolume; }
    void setMasterBalance (double balance) noexcept;
    double getMasterBalance() const noexcept  { return m_masterBalance; }
    void setMetronomeVolume (double volumeDb) noexcept;
    double getMetronomeVolume() const noexcept  { return m_metronome.m_volume; }
    void setMetronomeOn (bool on) noexcept      { m_metronome.m_on = on; }
    bool isMetronomeOn() const noexcept         { return m_metronome.m_on; }

    std::vector<MBoxEffect>& getMasterEffectChain() noexcept              { return m_masterEffectChain; }
    const std::vector<MBoxEffect>& getMasterEffectChain() const noexcept  { return m_masterEffectChain; }

private:
    int m_busId { 1 };
    juce::String m_name;
    std::vector<MBoxEffect> m_masterEffectChain;
    double m_masterVolume { 0.0 };
    double m_masterBalance { 0.0 };
    std::pair<int, int> m_outputs { 0, 1 };
    MBoxMetronome m_metronome;
};

//==============================================================================
// The audio device to open. Empty names mean the system's default device. A buffer size of
// 0 means the preferred size (32 samples where the device supports it).
//==============================================================================
struct MBoxDeviceSettings
{
    juce::String m_typeName;
    juce::String m_inputDeviceName;
    juce::String m_outputDeviceName;
    int m_bufferSize { 0 };

    bool isSystemDefault() const noexcept  { return m_typeName.isEmpty() && m_inputDeviceName.isEmpty() && m_outputDeviceName.isEmpty(); }
};

//==============================================================================
// Everything describing the rack itself rather than a song, saved in rig.json
//==============================================================================
struct MBoxRig
{
    MBoxRig();

    std::array<MBoxMixBus, numberOfMixBus> m_mixBuses;  // m_mixBuses[i] has id i + 1
    MBoxDeviceSettings m_device;
};

// Converts between a volume in dB and a linear gain. Volumes at or below minVolumeDb are silent.
float volumeDbToGain (double volumeDb) noexcept;
