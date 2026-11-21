#pragma once

#include <cstdint>
#include <vector>

//==============================================================================
// A click to play at an exact frame of a song (the frame where the click starts)
//==============================================================================
struct MBoxClickEvent
{
    std::int64_t m_frame { 0 };
    bool m_accent { false };

    bool operator< (const MBoxClickEvent& other) const noexcept  { return m_frame < other.m_frame; }
};

//==============================================================================
// The synthesised click sounds, rendered once into short mono buffers.
// - Metronome: a sine "beep" with a fast decay; the accented click (beat 1) is higher and louder.
// - Info: a two-tone "woodblock" with a sharper attack, so cue clicks are easy to tell apart
//   from the metronome when both play.
//==============================================================================
class MBoxClickSounds
{
public:
    explicit MBoxClickSounds (double sampleRate);

    const std::vector<float>& getMetronome (bool accent) const noexcept  { return accent ? m_metronomeAccent : m_metronome; }
    const std::vector<float>& getInfo (bool accent) const noexcept       { return accent ? m_infoAccent : m_info; }

    // Length of the longest click, in frames
    int getMaxLength() const noexcept;

    // Adds the part of every click overlapping [blockStart, blockStart + numFrames) into
    // destination. events must be sorted by frame. Allocation-free, for the audio thread.
    static void render (const std::vector<MBoxClickEvent>& events,
                        const std::vector<float>& normalSound, const std::vector<float>& accentSound,
                        std::int64_t blockStart, int numFrames, float* destination) noexcept;

private:
    std::vector<float> m_metronome, m_metronomeAccent, m_info, m_infoAccent;
};
