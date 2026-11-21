#pragma once

#include "ArrangementView.h"
#include "BrowserPanel.h"
#include "ControlSpaceMonitor.h"
#include "ListEditorPanel.h"
#include "MixerPanel.h"
#include "Session.h"
#include "SubmixPanel.h"
#include "TransportBar.h"

/** The OneBoxBand main window content:

        [                     transport bar                      ]
        [ browser ][ song arrangement           ][ list editor   ]
        [         ][ mixer                             ][ submixes ]
        [                       status bar                       ]

    The browser and the editor can be hidden (transport bar toggles, Cmd-1 / Cmd-2); the arrangement
    and the mixer take the room.

    Shortcuts: space play (from the edit cursor) / pause / resume, Ctrl-Space (re)start from the edit
    cursor, S stop, Cmd/Ctrl-S save all.
*/
class MainComponent final : public juce::Component,
                            private Session::Listener
{
public:
    explicit MainComponent (Session& session);
    ~MainComponent() override;

    /** For --snapshot: open the Banks or Audio device content in place of a window. */
    void showBanks();
    void showAudioDevice();
    /** Opens the Tuner window (it takes its input while open), or closes it. */
    void toggleTuner();
    void loadFolder();

    void setBrowserShown (bool shouldShow);
    void setEditorShown (bool shouldShow);

    void paint (juce::Graphics&) override;
    void resized() override;
    bool keyPressed (const juce::KeyPress&) override;

private:
    void sessionChanged() override;
    void showInList (Session::ListTab, const Bar&) override;

    Session& m_session;
    TransportBar m_transport;
    BrowserPanel m_browser;
    ArrangementView m_arrangement;
    ListEditorPanel m_editor;
    MixerPanel m_mixer;
    SubmixPanel m_submixes;

    bool m_browserShown = true;
    bool m_editorShown = true;

    std::unique_ptr<ui::Window> m_banksWindow, m_audioWindow, m_tunerWindow;
    std::unique_ptr<juce::FileChooser> m_folderChooser;
    juce::TooltipWindow m_tooltips { this, 600 };
    // Last, so it goes first: its callback uses the members above.
    ControlSpaceMonitor m_controlSpace { *this, [this] { m_session.playFromCursor(); } };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MainComponent)
};
