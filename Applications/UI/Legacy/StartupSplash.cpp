#include "StartupSplash.h"

StartupSplash::StartupSplash (const juce::String& version)
    : m_version ("Version " + version, ui::Label::Style::Caption)
{
    setLookAndFeel (&ui::LookAndFeel::getShared());
    setOpaque (true);

    for (auto* label : { &m_title, &m_version, &m_message })
        addAndMakeVisible (*label);

    m_message.setMinimumHorizontalScale (0.7f);
    setSize (460, 170);
}

void StartupSplash::show()
{
    addToDesktop (juce::ComponentPeer::windowHasDropShadow);
    centreWithSize (getWidth(), getHeight());
    setVisible (true);
    toFront (true);
}

StartupSplash::~StartupSplash()
{
    setLookAndFeel (nullptr);
}

void StartupSplash::setStep (const juce::String& message, double progress)
{
    m_message.setText (message, juce::dontSendNotification);
    m_progress = juce::jlimit (0.0, 1.0, progress);
    repaint();
}

void StartupSplash::paint (juce::Graphics& g)
{
    auto& theme = ui::getTheme (*this);
    const auto& palette = theme.getPalette();
    theme.drawBackground (g, getLocalBounds().toFloat());

    g.setColour (palette.panelBorder);
    g.drawRect (getLocalBounds(), 1);

    // The progress bar: a recessed track, filled in the accent colour.
    g.setColour (palette.arcTrack);
    g.fillRoundedRectangle (m_bar, m_bar.getHeight() * 0.5f);
    g.setColour (palette.panelBorder);
    g.drawRoundedRectangle (m_bar, m_bar.getHeight() * 0.5f, 1.0f);

    if (m_progress > 0.0)
    {
        const auto fill = m_bar.withWidth (juce::jmax (m_bar.getHeight(), m_bar.getWidth() * (float) m_progress));
        g.setGradientFill (juce::ColourGradient (palette.accent.brighter (0.2f), fill.getX(), fill.getY(),
                                                 palette.accent.darker (0.2f), fill.getX(), fill.getBottom(), false));
        g.fillRoundedRectangle (fill, fill.getHeight() * 0.5f);
    }
}

void StartupSplash::resized()
{
    auto area = getLocalBounds().reduced (28, 24);
    auto top = area.removeFromTop (34);
    m_version.setBounds (top.removeFromRight (120));
    m_version.setJustificationType (juce::Justification::centredRight);
    m_title.setBounds (top);

    area.removeFromTop (20);
    m_message.setBounds (area.removeFromTop (24));
    area.removeFromTop (14);
    m_bar = area.removeFromTop (8).toFloat();
}
