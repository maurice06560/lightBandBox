#include <juce_gui_basics/juce_gui_basics.h>

#include "Model/MBoxApplication.h"

class OneBoxBandApplication final : public juce::JUCEApplication
{
public:
    OneBoxBandApplication() = default;

    const juce::String getApplicationName() override       { return JUCE_APPLICATION_NAME_STRING; }
    const juce::String getApplicationVersion() override    { return JUCE_APPLICATION_VERSION_STRING; }
    bool moreThanOneInstanceAllowed() override             { return false; }

    void initialise (const juce::String&) override
    {
        m_application = std::make_unique<MBoxApplication>();

        // TO DELETE TEST
        m_application->testApplication();
    }

    void shutdown() override
    {
        m_application.reset();
    }

    void systemRequestedQuit() override
    {
        quit();
    }

    void anotherInstanceStarted (const juce::String&) override
    {
    }

private:
    std::unique_ptr<MBoxApplication> m_application;
};

START_JUCE_APPLICATION (OneBoxBandApplication)
