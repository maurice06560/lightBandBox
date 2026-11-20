#include "MBoxClickSynth.h"

#include <algorithm>
#include <cmath>

namespace
{
    constexpr double twoPi = 6.283185307179586;

    // A sum of sines with an exponential decay and a 1 ms attack, so the click itself doesn't click
    std::vector<float> synthesise (double sampleRate, double lengthSeconds, double decaySeconds, float level,
                                   std::initializer_list<std::pair<double, float>> partials)
    {
        std::vector<float> sound ((size_t) (lengthSeconds * sampleRate));
        const auto attackFrames = sampleRate * 0.001;

        for (size_t i = 0; i < sound.size(); ++i)
        {
            const auto time = (double) i / sampleRate;
            const auto envelope = std::min (1.0, (double) i / attackFrames) * std::exp (-time / decaySeconds);
            double value = 0.0;

            for (const auto& [frequency, amplitude] : partials)
                value += amplitude * std::sin (twoPi * frequency * time);

            sound[i] = (float) (value * envelope) * level;
        }

        return sound;
    }
}

//==============================================================================
MBoxClickSounds::MBoxClickSounds (double sampleRate)
    : m_metronome       (synthesise (sampleRate, 0.050, 0.010, 0.5f, { { 1000.0, 1.0f } })),
      m_metronomeAccent (synthesise (sampleRate, 0.050, 0.012, 0.7f, { { 1500.0, 1.0f } })),
      m_info            (synthesise (sampleRate, 0.040, 0.006, 0.5f, { { 700.0, 0.6f }, { 2400.0, 0.4f } })),
      m_infoAccent      (synthesise (sampleRate, 0.040, 0.008, 0.7f, { { 900.0, 0.6f }, { 3000.0, 0.4f } }))
{
}

int MBoxClickSounds::getMaxLength() const noexcept
{
    return (int) std::max ({ m_metronome.size(), m_metronomeAccent.size(), m_info.size(), m_infoAccent.size() });
}

//==============================================================================
// Every click that started before the block but is still sounding is included, so clicks
// spanning two blocks (or a pause) are rendered whole. Rendering depends only on the block
// position, so stopping, pausing and restarting need no state reset.
//==============================================================================
void MBoxClickSounds::render (const std::vector<MBoxClickEvent>& events,
                              const std::vector<float>& normalSound, const std::vector<float>& accentSound,
                              std::int64_t blockStart, int numFrames, float* destination) noexcept
{
    const auto maxLength = (std::int64_t) std::max (normalSound.size(), accentSound.size());
    const auto blockEnd = blockStart + numFrames;

    auto it = std::lower_bound (events.begin(), events.end(), MBoxClickEvent { blockStart - maxLength + 1 });

    for (; it != events.end() && it->m_frame < blockEnd; ++it)
    {
        const auto& sound = it->m_accent ? accentSound : normalSound;
        const auto soundStart = std::max (blockStart, it->m_frame);
        const auto soundEnd = std::min (blockEnd, it->m_frame + (std::int64_t) sound.size());

        for (auto frame = soundStart; frame < soundEnd; ++frame)
            destination[frame - blockStart] += sound[(size_t) (frame - it->m_frame)];
    }
}
