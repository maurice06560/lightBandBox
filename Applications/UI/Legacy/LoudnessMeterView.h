#pragma once

#include "UIHelpers.h"

#include <optional>

/** A vertical loudness meter in LUFS, styled like the mixer's meters: the momentary loudness as the
    bar, the short-term loudness as a thin line and the integrated loudness as a yellow marker, over
    a scale from minLufs to 0.
*/
class LoudnessMeterView final : public juce::Component,
                                public juce::SettableTooltipClient
{
public:
    static constexpr float minLufs = -60.0f;

    void setLevels (std::optional<float> momentary, std::optional<float> shortTerm, std::optional<float> integrated)
    {
        if (momentary == m_momentary && shortTerm == m_shortTerm && integrated == m_integrated)
            return;

        m_momentary = momentary;
        m_shortTerm = shortTerm;
        m_integrated = integrated;
        repaint();
    }

    void paint (juce::Graphics& g) override
    {
        const auto& palette = ui::getTheme (*this).getPalette();
        auto area = getLocalBounds().toFloat().reduced (0.0f, 6.0f);
        // Scale, bar and the integrated marker's arrow, centred together.
        area = area.withSizeKeepingCentre (juce::jmin (area.getWidth(), 24.0f + 4.0f + 18.0f + 10.0f), area.getHeight());
        const auto scale = area.removeFromLeft (24.0f);
        area.removeFromLeft (4.0f);
        const auto meter = area.removeFromLeft (18.0f);

        auto yFor = [&] (float lufs)
        {
            return meter.getY() + juce::jlimit (0.0f, 1.0f, lufs / minLufs) * meter.getHeight();
        };

        g.setFont (ui::LookAndFeel::makeFont (9.0f));

        // Every 6 LU when there is room, every 12 otherwise.
        const auto step = meter.getHeight() * 6.0f / -minLufs >= 16.0f ? 6 : 12;

        for (int lufs = 0; lufs >= (int) minLufs; lufs -= step)
        {
            const auto y = yFor ((float) lufs);
            g.setColour (palette.textDim.brighter (0.25f));
            g.drawText (juce::String (lufs), juce::Rectangle<float> (scale.getX(), y - 6.0f, scale.getWidth() - 6.0f, 12.0f),
                        juce::Justification::centredRight, false);
            g.setColour (palette.textDim);
            g.fillRect (scale.getRight() - 4.0f, y - 0.5f, 3.0f, 1.0f);
        }

        const auto body = meter.expanded (0.0f, 2.0f);
        g.setColour (palette.arcTrack);
        g.fillRoundedRectangle (body, 2.0f);
        g.setColour (palette.panelBorder);
        g.drawRoundedRectangle (body, 2.0f, 1.0f);

        const auto bar = meter.reduced (2.0f, 0.0f);

        if (m_momentary.has_value())
        {
            juce::ColourGradient gradient (palette.accentWarm, 0.0f, meter.getY(), palette.accent.darker (0.5f), 0.0f, meter.getBottom(), false);
            gradient.addColour (6.0 / -minLufs, palette.accent.brighter (0.3f));
            gradient.addColour (18.0 / -minLufs, palette.accent);
            g.setGradientFill (gradient);
            g.fillRect (bar.withTop (yFor (*m_momentary)));
        }

        if (m_shortTerm.has_value())
        {
            g.setColour (palette.text.brighter (0.4f));
            g.fillRect (bar.getX(), yFor (*m_shortTerm) - 1.0f, bar.getWidth(), 2.0f);
        }

        if (m_integrated.has_value())
        {
            const auto y = yFor (*m_integrated);
            g.setColour (palette.solo);
            g.fillRect (meter.getX() - 3.0f, y - 1.0f, meter.getWidth() + 6.0f, 2.0f);

            juce::Path arrow;
            arrow.addTriangle (meter.getRight() + 3.0f, y, meter.getRight() + 9.0f, y - 4.0f, meter.getRight() + 9.0f, y + 4.0f);
            g.fillPath (arrow);
        }
    }

private:
    std::optional<float> m_momentary, m_shortTerm, m_integrated;
};
