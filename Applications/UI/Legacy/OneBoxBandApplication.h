#pragma once

#include <onebox_ui/onebox_ui.h>

#include <memory>

class MBoxCoreAudio;
class Session;
class StartupSplash;

//==============================================================================
// The JUCE UI app. It owns the single MBoxCoreAudio instance (the library compiled in) and
// the main window.
//
// Startup runs in steps on the message loop so the splash can redraw between them: show the
// splash, create MBoxCoreAudio (loads the data folder, opens the audio device, starts the TCP
// server), then open the main window. If another app (the rack, or a second OneBoxBand) already
// runs the library, it says so and quits.
//==============================================================================
class OneBoxBandApplication final : public juce::JUCEApplication
{
public:
    OneBoxBandApplication();
    ~OneBoxBandApplication() override;

    const juce::String getApplicationName() override;
    const juce::String getApplicationVersion() override;
    bool moreThanOneInstanceAllowed() override  { return false; }

    void initialise (const juce::String& commandLine) override;
    void shutdown() override;

    void systemRequestedQuit() override;
    void anotherInstanceStarted (const juce::String& commandLine) override;

private:
    void createCore();
    void openMainWindow();

    std::unique_ptr<StartupSplash> m_splash;
    std::unique_ptr<MBoxCoreAudio> m_core;
    std::unique_ptr<Session> m_session;
    std::unique_ptr<onebox::ui::Window> m_mainWindow;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (OneBoxBandApplication)
};
