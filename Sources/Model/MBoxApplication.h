#pragma once

#include "../Audio/MBoxAudioEngine.h"

class MBoxApplication final
{
public:
    MBoxApplication() = default;
    ~MBoxApplication() = default;

private:
    MBoxAudioEngine m_audioEngine;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MBoxApplication)
};
