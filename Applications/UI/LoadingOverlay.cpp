#include "LoadingOverlay.h"

namespace
{
    constexpr int pollHz = 20;
    constexpr int showDelayTicks = 3;   // 150 ms
}

LoadingOverlay::LoadingOverlay (Session& session)
    : m_session (session)
{
    for (auto* label : { &m_title, &m_message })
        addAndMakeVisible (*label);

    m_message.setMinimumHorizontalScale (0.7f);
    setWantsKeyboardFocus (true);
    setVisible (false);
    m_session.addListener (this);
    startTimerHz (pollHz);
}

LoadingOverlay::~LoadingOverlay()
{
    m_session.removeListener (this);

    if (isCurrentlyModal())
        exitModalState (0);
}

//==============================================================================
void LoadingOverlay::sessionLoadStarted()
{
    const auto info = m_session.getCore().getTransportInfo();

    if (! info.m_loading)
        return;

    m_loadingTicks = showDelayTicks;
    update (info);

    // Painted now: the panels refresh for the new song before the message loop runs again
    if (auto* peer = getPeer())
        peer->performAnyPendingRepaintsNow();
}

void LoadingOverlay::timerCallback()
{
    const auto info = m_session.getCore().getTransportInfo();

    if (! info.m_loading)
    {
        m_loadingTicks = 0;
        hide();
        return;
    }

    if (++m_loadingTicks < showDelayTicks)
        return;

    update (info);
}

void LoadingOverlay::update (const MBoxTransportInfo& info)
{
    const auto percent = juce::roundToInt (info.m_loadingProgress * 100.0f);
    m_message.setText (juce::String (info.m_loadingPlaylist) + "... " + juce::String (percent) + "%", juce::dontSendNotification);
    m_progress = juce::jlimit (0.0, 1.0, (double) info.m_loadingProgress);
    show();
    repaint (m_card);
}

void LoadingOverlay::show()
{
    if (isVisible())
        return;

    setVisible (true);
    toFront (true);
    enterModalState (true);
}

void LoadingOverlay::hide()
{
    if (! isVisible())
        return;

    if (isCurrentlyModal())
        exitModalState (0);

    setVisible (false);

    if (auto* parent = getParentComponent())
        parent->grabKeyboardFocus();   // the main window's shortcuts work again
}

//==============================================================================
void LoadingOverlay::paint (juce::Graphics& g)
{
    auto& theme = ui::getTheme (*this);
    const auto& palette = theme.getPalette();

    g.fillAll (juce::Colours::black.withAlpha (0.55f));

    // The card and its progress bar, like the startup splash
    theme.drawBackground (g, m_card.toFloat());
    g.setColour (palette.panelBorder);
    g.drawRect (m_card, 1);

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

void LoadingOverlay::resized()
{
    m_card = getLocalBounds().withSizeKeepingCentre (460, 150);

    auto area = m_card.reduced (28, 24);
    m_title.setBounds (area.removeFromTop (34));
    area.removeFromTop (12);
    m_message.setBounds (area.removeFromTop (24));
    area.removeFromTop (14);
    m_bar = area.removeFromTop (8).toFloat();
}
