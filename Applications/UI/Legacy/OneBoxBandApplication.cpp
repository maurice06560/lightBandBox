#include "OneBoxBandApplication.h"

#include "Session.h"
#include "StartupSplash.h"

#include <MBoxCoreAudio.h>

namespace
{
    //==========================================================================
    // TEMPORARY main window content until MainComponent is ported (UIPortPlan.md, step 3):
    // shows the library's startup result and the Session's selection, transport and status.
    class PlaceholderComponent final : public juce::Component,
                                       private Session::Listener
    {
    public:
        explicit PlaceholderComponent (Session& session) : m_session (session)
        {
            addAndMakeVisible (m_title);

            // One label per line: the theme sizes a label's font from its height
            for (auto& line : m_lines)
                addAndMakeVisible (line);

            m_session.addListener (this);
            refresh();
        }

        ~PlaceholderComponent() override  { m_session.removeListener (this); }

        void resized() override
        {
            auto area = getLocalBounds().reduced (24);
            m_title.setBounds (area.removeFromTop (40));
            area.removeFromTop (12);

            for (auto& line : m_lines)
                line.setBounds (area.removeFromTop (26));
        }

    private:
        void sessionChanged() override           { refresh(); }
        void sessionTransportChanged() override  { refresh(); }

        void refresh()
        {
            auto& core = m_session.getCore();
            const auto result = core.getInitialisationResult();
            const auto device = core.getCurrentDevice();
            const auto& transport = m_session.getTransport();

            juce::StringArray lines;
            lines.add (result.wasOk() ? "Library started." : "Library started with errors: " + juce::String (result.m_message));
            lines.add ("Audio device: " + (device.m_outputDevice.empty() ? juce::String ("none")
                                                                           : juce::String (device.m_outputDevice) + " ("
                                                                                 + juce::String (device.m_bufferSize) + " samples)"));
            lines.add ("Playlists: " + m_session.getPlaylistNames().joinIntoString (", "));
            lines.add ("Selected: " + m_session.getSelectedPlaylistName() + " / " + m_session.getSelectedSongName()
                       + (m_session.isSelectionLoaded() ? " (loaded)" : ""));
            lines.add ("Transport: " + juce::String (transport.m_state) + " at " + juce::String (transport.m_position));
            lines.add ("Status: " + m_session.getStatus());

            for (size_t i = 0; i < m_lines.size(); ++i)
                m_lines[i].setText (lines[(int) i], juce::dontSendNotification);
        }

        Session& m_session;
        onebox::ui::Label m_title { "OneBoxBand: UI port in progress", onebox::ui::Label::Style::Title };
        std::array<onebox::ui::Label, 6> m_lines;
    };
}

//==============================================================================
OneBoxBandApplication::OneBoxBandApplication()  = default;
OneBoxBandApplication::~OneBoxBandApplication() = default;

const juce::String OneBoxBandApplication::getApplicationName()    { return JUCE_APPLICATION_NAME_STRING; }
const juce::String OneBoxBandApplication::getApplicationVersion() { return JUCE_APPLICATION_VERSION_STRING; }

//==============================================================================
void OneBoxBandApplication::initialise (const juce::String&)
{
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

    m_splash->setStep ("Opening the main window...", 0.9);
    juce::MessageManager::callAsync ([this] { openMainWindow(); });
}

void OneBoxBandApplication::openMainWindow()
{
    m_mainWindow = std::make_unique<onebox::ui::Window> (getApplicationName());
    m_mainWindow->setContent (std::make_unique<PlaceholderComponent> (*m_session), 900, 600);
    m_mainWindow->setVisible (true);

    m_splash.reset();
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
