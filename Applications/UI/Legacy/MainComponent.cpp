#include "MainComponent.h"
#include "AudioDevicePanel.h"
#include "BanksEditor.h"
#include "TunerPanel.h"

namespace
{
    constexpr int transportHeight = 64;
    constexpr int statusHeight = 26;
    constexpr int browserWidth = 240;
    constexpr int editorWidth = 400;
    constexpr int mixerHeight = 340;
    constexpr int gap = 8;
}

MainComponent::MainComponent (Session& session)
    : m_session (session), m_transport (session), m_browser (session), m_arrangement (session), m_editor (session), m_mixer (session), m_submixes (session)
{
    for (auto* c : std::initializer_list<juce::Component*> { &m_transport, &m_browser, &m_arrangement, &m_editor, &m_mixer, &m_submixes })
        addAndMakeVisible (c);

    m_transport.onShowBanks = [this] { showBanks(); };
    m_transport.onShowAudioDevice = [this] { showAudioDevice(); };
    m_transport.onLoadFolder = [this] { loadFolder(); };
    m_transport.onToggleTuner = [this] { toggleTuner(); };
    m_transport.onToggleBrowser = [this] { setBrowserShown (! m_browserShown); };
    m_transport.onToggleEditor = [this] { setEditorShown (! m_editorShown); };
    m_transport.setPanelsShown (m_browserShown, m_editorShown);

    setWantsKeyboardFocus (true);
    m_session.addListener (this);
}

MainComponent::~MainComponent()
{
    m_session.removeListener (this);
}

void MainComponent::paint (juce::Graphics& g)
{
    const auto& palette = ui::getTheme (*this).getPalette();

    // Status bar
    auto status = getLocalBounds().removeFromBottom (statusHeight);
    g.setColour (palette.panelFill.darker (0.2f));
    g.fillRect (status);
    g.setColour (juce::Colours::black.withAlpha (0.6f));
    g.fillRect (status.removeFromTop (1));

    status.reduce (14, 0);
    g.setFont (ui::LookAndFeel::makeFont (12.0f));
    g.setColour (palette.textDim.brighter (0.2f));
    const auto path = m_session.getApp().getPaths().getRoot().getFullPathName();
    const auto font = ui::LookAndFeel::makeFont (12.0f);
    const auto pathWidth = juce::jmin (status.getWidth() / 3, juce::roundToInt (juce::GlyphArrangement::getStringWidth (font, path)) + 4);
    g.drawText (path, status.removeFromRight (pathWidth), juce::Justification::centredRight, true);

    // The selected mix's latency, just left of the data folder.
    if (m_session.getSelectedSong() != nullptr)
    {
        juce::String latency = "Latency: ";

        if (const auto mix = m_session.getSelectedMixLatency(); ! mix.has_value())
            latency << "-";
        else if (! mix->routed)
            latency << "N/A (no outputs)";
        else
            latency << juce::String (mix->milliseconds, 1) << " ms" << (mix->fromInput ? "" : " (playback)");

        status.removeFromRight (10);
        g.setColour (palette.textDim.withAlpha (0.6f));
        g.fillRect (status.removeFromRight (1).withSizeKeepingCentre (1, 12));
        status.removeFromRight (10);
        g.setColour (palette.text);
        g.drawText (latency, status.removeFromRight (170), juce::Justification::centredRight, false);
    }

    const auto message = m_session.getStatus();

    if (message.isNotEmpty())
    {
        auto dot = status.removeFromLeft (10).toFloat().withSizeKeepingCentre (7.0f, 7.0f);
        g.setColour (m_session.isStatusError() ? palette.accentWarm : palette.accent);
        g.fillEllipse (dot);
        status.removeFromLeft (8);
        g.setColour (m_session.isStatusError() ? palette.accentWarm.brighter (0.2f) : palette.text);
        g.drawText (message, status.withTrimmedRight (status.getWidth() / 3), juce::Justification::centredLeft, true);
    }

    // Recessed frame around the arrangement, matching the panels
    const auto frame = m_arrangement.getBounds().toFloat().expanded (1.0f);
    g.setColour (palette.panelBorder);
    g.drawRoundedRectangle (frame, 4.0f, 1.0f);
}

void MainComponent::resized()
{
    auto area = getLocalBounds();
    m_transport.setBounds (area.removeFromTop (transportHeight));
    area.removeFromBottom (statusHeight);
    area.reduce (gap, gap);

    m_browser.setVisible (m_browserShown);
    m_editor.setVisible (m_editorShown);

    if (m_browserShown)
    {
        m_browser.setBounds (area.removeFromLeft (browserWidth));
        area.removeFromLeft (gap);
    }

    auto mixerRow = area.removeFromBottom (juce::jmin (mixerHeight, area.getHeight() / 2));
    m_submixes.setBounds (mixerRow.removeFromRight (SubmixPanel::preferredWidth));
    mixerRow.removeFromRight (gap);
    m_mixer.setBounds (mixerRow);
    area.removeFromBottom (gap);

    if (m_editorShown)
    {
        m_editor.setBounds (area.removeFromRight (editorWidth));
        area.removeFromRight (gap);
    }

    m_arrangement.setBounds (area.reduced (1));
}

bool MainComponent::keyPressed (const juce::KeyPress& key)
{
    // Space: play / pause (a paused song resumes where it paused). Ctrl + Space: (re)start from the
    // edit cursor, whether stopped, playing or paused.
    if (key == juce::KeyPress (juce::KeyPress::spaceKey, juce::ModifierKeys::ctrlModifier, 0))
    {
        m_session.playFromCursor();
        return true;
    }

    if (key == juce::KeyPress::spaceKey)
    {
        m_transport.togglePlayPause();
        return true;
    }

    if (key.getTextCharacter() == 's' && ! key.getModifiers().isCommandDown())
    {
        m_transport.stop();
        return true;
    }

    if (key == juce::KeyPress ('s', juce::ModifierKeys::commandModifier, 0))
    {
        m_session.saveAll();
        return true;
    }

    // Copy / paste a playlist: the selected playlist row (not a song's).
    if (key == juce::KeyPress ('c', juce::ModifierKeys::commandModifier, 0))
    {
        if (m_session.getSelectedSongName().isEmpty() && m_session.getSelectedPlaylistName().isNotEmpty())
            m_session.copyPlaylist (m_session.getSelectedPlaylistName());

        return true;
    }

    if (key == juce::KeyPress ('v', juce::ModifierKeys::commandModifier, 0))
    {
        if (m_session.canPastePlaylist())
            m_session.pastePlaylist();

        return true;
    }

    if (key == juce::KeyPress ('1', juce::ModifierKeys::commandModifier, 0))
    {
        setBrowserShown (! m_browserShown);
        return true;
    }

    if (key == juce::KeyPress ('2', juce::ModifierKeys::commandModifier, 0))
    {
        setEditorShown (! m_editorShown);
        return true;
    }

    return false;
}

void MainComponent::sessionChanged()
{
    repaint (getLocalBounds().removeFromBottom (statusHeight));
}

void MainComponent::showInList (Session::ListTab, const Bar&)
{
    // The editor panel switches tab itself; it just has to be visible to show it.
    setEditorShown (true);
}

void MainComponent::setBrowserShown (bool shouldShow)
{
    if (shouldShow == m_browserShown)
        return;

    m_browserShown = shouldShow;
    m_transport.setPanelsShown (m_browserShown, m_editorShown);
    resized();
    repaint();
}

void MainComponent::setEditorShown (bool shouldShow)
{
    if (shouldShow == m_editorShown)
        return;

    m_editorShown = shouldShow;
    m_transport.setPanelsShown (m_browserShown, m_editorShown);
    resized();
    repaint();
}

//==============================================================================
void MainComponent::showBanks()
{
    if (m_banksWindow == nullptr)
    {
        m_banksWindow = std::make_unique<ui::Window> ("Banks", true, true);
        m_banksWindow->setContent (std::make_unique<BanksEditor> (m_session), 980, 660);
        m_banksWindow->onCloseButtonPressed = [this] { m_banksWindow->setVisible (false); };
    }

    m_banksWindow->setVisible (true);
    m_banksWindow->toFront (true);
}

void MainComponent::showAudioDevice()
{
    if (m_audioWindow == nullptr)
    {
        m_audioWindow = std::make_unique<ui::Window> ("Audio device", false, true);
        m_audioWindow->setContent (std::make_unique<AudioDevicePanel> (m_session), 580, 660);
        m_audioWindow->onCloseButtonPressed = [this] { m_audioWindow->setVisible (false); };
    }
    else if (auto* panel = dynamic_cast<AudioDevicePanel*> (m_audioWindow->getContent()))
    {
        panel->refresh();
    }

    m_audioWindow->setVisible (true);
    m_audioWindow->toFront (true);
}

void MainComponent::toggleTuner()
{
    if (m_tunerWindow == nullptr)
    {
        m_tunerWindow = std::make_unique<ui::Window> ("Tuner", false, true);
        m_tunerWindow->setContent (std::make_unique<TunerPanel> (m_session), 460, 340);
        m_tunerWindow->onCloseButtonPressed = [this] { toggleTuner(); };
    }

    auto* panel = dynamic_cast<TunerPanel*> (m_tunerWindow->getContent());
    const auto open = ! m_tunerWindow->isVisible();

    // The input goes back to the outputs as soon as the window closes.
    if (open)
    {
        m_tunerWindow->setVisible (true);
        m_tunerWindow->toFront (true);
        panel->start();
    }
    else
    {
        panel->stop();
        m_tunerWindow->setVisible (false);
    }

    m_transport.setTunerShown (open);
}

void MainComponent::loadFolder()
{
    auto proceed = [this]
    {
        m_folderChooser = std::make_unique<juce::FileChooser> ("Load a OneBox data folder", m_session.getApp().getPaths().getRoot());
        m_folderChooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories,
                                      [this] (const juce::FileChooser& chooser)
                                      {
                                          if (const auto folder = chooser.getResult(); folder.isDirectory())
                                              m_session.loadFolder (folder);
                                      });
    };

    if (! m_session.hasUnsavedChanges())
    {
        proceed();
        return;
    }

    UIHelpers::ask ("Unsaved changes", "Save your changes before loading another folder?", { "Save", "Don't save", "Cancel" },
                    [this, proceed] (int choice)
                    {
                        if (choice == 2)
                            return;

                        if (choice == 0 && m_session.saveAll().failed())
                            return;

                        proceed();
                    });
}
