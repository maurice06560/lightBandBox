#include "OneBoxBandApplication.h"

#include "MainComponent.h"
#include "Session.h"
#include "StartupSplash.h"

#include <MBoxCoreAudio.h>

//==============================================================================
OneBoxBandApplication::OneBoxBandApplication()  = default;
OneBoxBandApplication::~OneBoxBandApplication() = default;

const juce::String OneBoxBandApplication::getApplicationName()    { return JUCE_APPLICATION_NAME_STRING; }
const juce::String OneBoxBandApplication::getApplicationVersion() { return JUCE_APPLICATION_VERSION_STRING; }

//==============================================================================
void OneBoxBandApplication::initialise (const juce::String& commandLine)
{
    const auto arguments = juce::StringArray::fromTokens (commandLine, true);

    if (const auto index = arguments.indexOf ("--snapshot"); index >= 0 && index + 1 < arguments.size())
        m_snapshotFile = juce::File::getCurrentWorkingDirectory().getChildFile (arguments[index + 1].unquoted());

    m_snapshotBanks = arguments.contains ("--banks");

    m_splash = std::make_unique<StartupSplash> (getApplicationVersion());
    m_splash->show();
    m_splash->setStep ("Loading the data folder and opening the audio device...", 0.2);

    // Lets the splash paint before the (blocking) library startup
    juce::MessageManager::callAsync ([this] { createCore(); });
}

//==============================================================================
void OneBoxBandApplication::createCore()
{
    m_core = std::make_unique<MBoxCoreAudio>();

    const auto result = m_core->getInitialisationResult();

    // MBoxCoreAudio reports another running instance as "Another ... already ..."
    if (! result.wasOk() && result.m_message.rfind ("Another", 0) == 0)
    {
        m_core.reset();
        m_splash.reset();

        juce::AlertWindow::showAsync (juce::MessageBoxOptions()
                                          .withIconType (juce::MessageBoxIconType::WarningIcon)
                                          .withTitle (getApplicationName())
                                          .withMessage (juce::String (result.m_message)
                                                        + ".\n\nQuit the rack (or the other OneBoxBand) first.")
                                          .withButton ("Quit", 0),
                                      [] (int) { quit(); });
        setApplicationReturnValue (1);
        return;
    }

    m_session = std::make_unique<Session> (*m_core);
    m_session->selectFirstSong();  // loads its playlist in the background
    openMainWindowWhenLoaded();
}

// The splash shows the first playlist's loading; the main window opens once it's ready
void OneBoxBandApplication::openMainWindowWhenLoaded()
{
    if (m_splash == nullptr || m_core == nullptr)
        return;  // quitting

    const auto info = m_core->getTransportInfo();

    if (info.m_loading)
    {
        m_splash->setStep ("Loading " + juce::String (info.m_loadingPlaylist) + "...", 0.3 + 0.6 * info.m_loadingProgress);
        juce::Timer::callAfterDelay (50, [this] { openMainWindowWhenLoaded(); });
        return;
    }

    m_splash->setStep ("Opening the main window...", 0.95);
    juce::MessageManager::callAsync ([this] { openMainWindow(); });
}

void OneBoxBandApplication::openMainWindow()
{
    m_mainWindow = std::make_unique<onebox::ui::Window> (getApplicationName());
    m_mainWindow->setContent (std::make_unique<MainComponent> (*m_session), 1440, 900);
    m_mainWindow->setVisible (true);

    m_splash.reset();

    if (m_snapshotFile != juce::File())
        takeSnapshotWhenReady (100);
}

void OneBoxBandApplication::takeSnapshotWhenReady (int attemptsLeft)
{
    // Every 100 ms until the selected song is loaded, then once more so the panels catch up
    const auto ready = m_session->getSelectedSong() == nullptr || m_session->isSelectionLoaded();

    if (! ready && attemptsLeft > 0)
    {
        juce::Timer::callAfterDelay (100, [this, attemptsLeft] { takeSnapshotWhenReady (attemptsLeft - 1); });
        return;
    }

    auto* main = dynamic_cast<MainComponent*> (m_mainWindow->getContent());

    if (m_snapshotBanks && main != nullptr)
        main->showBanks();

    juce::Timer::callAfterDelay (500, [this, main]
    {
        juce::Component* content = m_snapshotBanks && main != nullptr ? main->getBanksContent() : m_mainWindow->getContent();
        const auto image = content->createComponentSnapshot (content->getLocalBounds(), true, 1.0f);

        m_snapshotFile.deleteFile();
        juce::FileOutputStream stream (m_snapshotFile);
        juce::PNGImageFormat png;

        if (! stream.openedOk() || ! png.writeImageToStream (image, stream))
            setApplicationReturnValue (1);

        quit();
    });
}

//==============================================================================
void OneBoxBandApplication::shutdown()
{
    m_mainWindow.reset();  // the UI goes before the library it shows
    m_splash.reset();
    m_session.reset();

    if (m_core != nullptr)
    {
        m_core->save();
        m_core.reset();
    }
}

//==============================================================================
void OneBoxBandApplication::systemRequestedQuit()
{
    quit();
}

void OneBoxBandApplication::anotherInstanceStarted (const juce::String&)
{
    if (m_mainWindow != nullptr)
        m_mainWindow->toFront (true);
}
