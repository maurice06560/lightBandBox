#pragma once

#include "Session.h"
#include "UIHelpers.h"

/** Shown over the main window while the library loads or rebuilds a playlist: a dimmed layer
    with a card giving the playlist's name and the progress, like the startup splash.

    While it shows it is modal, so the app's other windows (Banks, audio device, plugin editors)
    ignore the mouse and the keyboard too. Playback carries on. Selecting a song of another
    playlist shows it at once, until the playlist is loaded. Otherwise it polls the library and
    only appears after a short delay, so that quick rebuilds (no plugins) don't flash it.
*/
class LoadingOverlay final : public juce::Component,
                             private Session::Listener,
                             private juce::Timer
{
public:
    explicit LoadingOverlay (Session& session);
    ~LoadingOverlay() override;

    void paint (juce::Graphics&) override;
    void resized() override;
    bool keyPressed (const juce::KeyPress&) override  { return true; }   // swallows every key

private:
    void sessionChanged() override {}
    void sessionLoadStarted() override;
    void timerCallback() override;
    void update (const MBoxTransportInfo& info);
    void inputAttemptWhenModal() override {}   // no beep for clicks elsewhere
    void show();
    void hide();

    Session& m_session;
    ui::Label m_title { "Loading", ui::Label::Style::Title };
    ui::Label m_message { {}, ui::Label::Style::Body };
    juce::Rectangle<int> m_card;
    juce::Rectangle<float> m_bar;
    double m_progress = 0.0;
    int m_loadingTicks = 0;   // timer ticks since the load started

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (LoadingOverlay)
};
