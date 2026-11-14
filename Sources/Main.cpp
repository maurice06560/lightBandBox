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
        const auto audioFolder = getAudioFolder();
        m_application = std::make_unique<MBoxApplication> (audioFolder);
        m_application->loadPlaylists();

        //m_application->play();
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
    /** Returns $LIGHTBOX_DIR if it is set, otherwise the default "/Users/mmarin/OneBox/". */
    static juce::File getAudioFolder()
    {
        const auto lightboxDir = juce::SystemStats::getEnvironmentVariable ("LIGHTBOX_DIR", {});

        if (lightboxDir.isNotEmpty())
            return juce::File (lightboxDir);

        return juce::File ("/Users/mmarin/OneBox/");
    }

    std::unique_ptr<MBoxApplication> m_application;
};

START_JUCE_APPLICATION (OneBoxBandApplication)
