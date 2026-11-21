#pragma once

#include "ArrangementView.h"
#include "BrowserPanel.h"
#include "ControlSpaceMonitor.h"
#include "ListEditorPanel.h"
#include "LoadingOverlay.h"
#include "MixerPanel.h"
#include "PluginWindows.h"
#include "Session.h"
#include "SubmixPanel.h"
#include "TransportBar.h"

/** The OneBoxBand main window content:

        [                     transport bar                      ]
        [ browser ][ song arrangement           ][ list editor   ]
        [         ][ mixer                             ][ submixes ]
        [                       status bar                       ]

    While a playlist loads, a modal overlay shows its progress and blocks the rest of the app.

    The browser and the editor can be hidden (transport bar toggles, Cmd-1 / Cmd-2); the arrangement
    and the mixer take the room.

    Shortcuts: space play (from the edit cursor) / pause / resume, Ctrl-Space (re)start from the
    edit cursor, S stop, Cmd/Ctrl-S save now (everything is also saved about a second after each
    change), Cmd-C / Cmd-V copy and paste the selected playlist.
*/
class MainComponent final : public juce::Component,
                            private Session::Listener
{
public:
    explicit MainComponent (Session& session);
    ~MainComponent() override;

    void showAudioDevice();
    void showBanks();
    /** The Banks window's content, once it has been shown (for snapshots). */
    juce::Component* getBanksContent() const  { return m_banksWindow != nullptr ? m_banksWindow->getContent() : nullptr; }

    void setBrowserShown (bool shouldShow);
    void setEditorShown (bool shouldShow);

    void paint (juce::Graphics&) override;
    void resized() override;
    bool keyPressed (const juce::KeyPress&) override;

private:
    void sessionChanged() override;
    void showInList (Session::ListTab, const Bar&) override;

    Session& m_session;
    PluginWindows m_pluginWindows { m_session };   // before the panels that open them
    TransportBar m_transport;
    BrowserPanel m_browser;
    ArrangementView m_arrangement;
    ListEditorPanel m_editor;
    MixerPanel m_mixer;
    SubmixPanel m_submixes;
    LoadingOverlay m_loadingOverlay { m_session };   // over everything, while a playlist loads

    bool m_browserShown = true;
    bool m_editorShown = true;

    std::unique_ptr<ui::Window> m_audioWindow, m_banksWindow;
    juce::TooltipWindow m_tooltips { this, 600 };
    // Last, so it goes first: its callback uses the members above.
    ControlSpaceMonitor m_controlSpace { *this, [this] { m_session.playFromCursor(); } };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MainComponent)
};
