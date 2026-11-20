#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <memory>

class MBoxCoreAudio;

//==============================================================================
// The headless rack app: creates the single MBoxCoreAudio instance (from the MBoxCore
// shared library) on the message thread and keeps it alive until the app quits.
// Library events are written to the log; clients drive it over TCP.
// SIGINT and SIGTERM quit cleanly (through shutdown(), which saves the data folder).
//==============================================================================
class OneBoxBandApplication final : public juce::JUCEApplication,
                                    private juce::Timer
{
public:
    OneBoxBandApplication();
    ~OneBoxBandApplication() override;

    const juce::String getApplicationName() override;
    const juce::String getApplicationVersion() override;

    // MBoxCoreAudio refuses a second instance anyway; this makes the OS bring back the first.
    bool moreThanOneInstanceAllowed() override { return false; }

    // Quits with a non-zero exit code if the library can't take the single-instance lock.
    void initialise (const juce::String& commandLine) override;
    void shutdown() override;

    void systemRequestedQuit() override;
    void anotherInstanceStarted (const juce::String& commandLine) override;

private:
    // Polls the flag set by the signal handler (which can't safely call quit() itself)
    void timerCallback() override;

    std::unique_ptr<MBoxCoreAudio> m_core;
    int m_eventListenerId { -1 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (OneBoxBandApplication)
};
