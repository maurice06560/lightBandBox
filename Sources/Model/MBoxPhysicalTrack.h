#pragma once

#include "MBoxTrack.h"

/** A track fed by a physical input of the audio interface. */
class MBoxPhysicalTrack final : public MBoxTrack
{
public:
    MBoxPhysicalTrack() = default;
    explicit MBoxPhysicalTrack (int physicalInput) : m_physicalInput (physicalInput) {}
    ~MBoxPhysicalTrack() override = default;

    // Getters
    int getPhysicalInput() const { return m_physicalInput; }

private:
    int m_physicalInput { 0 };      // Input of the audio interface
};
