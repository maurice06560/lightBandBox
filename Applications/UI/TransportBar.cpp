#include "TransportBar.h"
#include "TempoMap.h"

TransportBar::TransportBar (Session& session)
    : m_session (session)
{
    for (auto* c : std::initializer_list<juce::Component*> { &m_browserToggle, &m_editorToggle, &m_play, &m_stop, &m_metronome, &m_songTitle, &m_songCaption,
                                                              &m_position, &m_time, &m_tempo, &m_signature,
                                                              &m_positionCaption, &m_timeCaption, &m_tempoCaption, &m_signatureCaption,
                                                              &m_save, &m_banks, &m_audio })
        addAndMakeVisible (c);

    for (auto* caption : { &m_positionCaption, &m_timeCaption, &m_tempoCaption, &m_signatureCaption })
        caption->setJustificationType (juce::Justification::centred);

    m_browserToggle.setTooltip ("Show / hide the playlists (Cmd-1)");
    m_editorToggle.setTooltip ("Show / hide the editor (Cmd-2)");
    m_browserToggle.onClick = [this] { if (onToggleBrowser != nullptr) onToggleBrowser(); };
    m_editorToggle.onClick = [this] { if (onToggleEditor != nullptr) onToggleEditor(); };

    m_play.setTooltip ("Play from the edit cursor / pause / resume (space); Ctrl-Space restarts from the edit cursor");
    m_stop.setTooltip ("Stop (S); the next play starts again from the edit cursor");
    m_metronome.setTooltip ("Metronome on/off for this song (the count-in only setting is in the Song editor)");
    m_save.setTooltip ("Changes are saved automatically about a second later; this saves at once (Cmd-S)");
    m_banks.setTooltip ("Edit the banks and their effect chains");
    m_audio.setTooltip ("Choose the audio device");

    m_play.onClick = [this] { togglePlayPause(); };
    m_stop.onClick = [this] { stop(); };
    m_metronome.onClick = [this]
    {
        const auto on = m_metronome.getToggleState();

        if (m_session.setMetronomeState (on ? MetronomeState::On : MetronomeState::Off).failed())
            sessionChanged();   // put the button back
    };

    m_save.onClick  = [this] { m_session.saveAll(); };
    m_banks.onClick = [this] { if (onShowBanks != nullptr) onShowBanks(); };
    m_audio.onClick = [this] { if (onShowAudioDevice != nullptr) onShowAudioDevice(); };

    m_session.addListener (this);
    sessionChanged();
    startTimerHz (30);
}

TransportBar::~TransportBar()
{
    m_session.removeListener (this);
}

void TransportBar::setPanelsShown (bool browser, bool editor)
{
    m_browserToggle.setToggleState (browser, juce::dontSendNotification);
    m_editorToggle.setToggleState (editor, juce::dontSendNotification);
}

void TransportBar::togglePlayPause()
{
    if (m_session.isPlaying())
        m_session.pause();
    else
        m_session.play();
}

void TransportBar::stop()
{
    m_session.stop();
}

void TransportBar::paint (juce::Graphics& g)
{
    const auto& palette = ui::getTheme (*this).getPalette();
    const auto area = getLocalBounds().toFloat();

    g.setGradientFill (juce::ColourGradient (palette.bgTop.brighter (0.06f), 0.0f, 0.0f,
                                             palette.panelFill, 0.0f, area.getBottom(), false));
    g.fillRect (area);
    g.setColour (juce::Colours::black.withAlpha (0.6f));
    g.fillRect (area.withTop (area.getBottom() - 1.0f));
}

void TransportBar::resized()
{
    auto area = getLocalBounds().reduced (14, 10);

    m_browserToggle.setBounds (area.removeFromLeft (48));
    area.removeFromLeft (16);
    m_editorToggle.setBounds (area.removeFromRight (48));
    area.removeFromRight (16);

    m_play.setBounds (area.removeFromLeft (64));
    area.removeFromLeft (6);
    m_stop.setBounds (area.removeFromLeft (52));
    area.removeFromLeft (16);

    auto actions = area.removeFromRight (4 * 78 + 3 * 6);
    for (auto* b : { &m_save, &m_banks, &m_audio })
    {
        b->setBounds (actions.removeFromLeft (78));
        actions.removeFromLeft (6);
    }

    area.removeFromRight (16);
    m_metronome.setBounds (area.removeFromRight (52));
    area.removeFromRight (16);

    // Readouts in the centre, captions underneath.
    const int readoutWidths[] { 130, 110, 110, 80 };
    const auto readoutsWidth = 130 + 110 + 110 + 80 + 3 * 8;
    auto readouts = area.removeFromRight (readoutsWidth);
    area.removeFromRight (16);

    juce::Component* values[] { &m_position, &m_time, &m_tempo, &m_signature };
    juce::Component* captions[] { &m_positionCaption, &m_timeCaption, &m_tempoCaption, &m_signatureCaption };

    for (int i = 0; i < 4; ++i)
    {
        auto column = readouts.removeFromLeft (readoutWidths[i]);
        readouts.removeFromLeft (8);
        captions[i]->setBounds (column.removeFromBottom (14));
        values[i]->setBounds (column.reduced (0, 2));
    }

    m_songTitle.setBounds (area.removeFromTop (area.getHeight() * 3 / 5));
    m_songCaption.setBounds (area);
}

//==============================================================================
void TransportBar::sessionChanged()
{
    auto* song = m_session.getSelectedSong();

    if (song != nullptr)
    {
        m_songTitle.setText (song->getName(), juce::dontSendNotification);
        m_songCaption.setText (m_session.getSelectedPlaylistName() + "  |  " + juce::String ((int) song->getTracks().size())
                                   + " track(s), " + juce::String (song->getTotalBar()) + " bars"
                                   + (m_session.isSelectionLoaded() ? juce::String() : juce::String ("  |  loading...")),
                               juce::dontSendNotification);
        m_metronome.setToggleState (song->getMetronomeState() != MetronomeState::Off, juce::dontSendNotification);
    }
    else
    {
        m_songTitle.setText ("No song selected", juce::dontSendNotification);
        m_songCaption.setText ("Create or select a song in the playlist browser", juce::dontSendNotification);
    }

    for (auto* c : std::initializer_list<juce::Component*> { &m_play, &m_stop, &m_metronome })
        c->setEnabled (song != nullptr);

    updateReadouts();
}

void TransportBar::timerCallback()
{
    updateReadouts();
}

void TransportBar::updateReadouts()
{
    auto* song = m_session.getSelectedSong();
    const auto playhead = m_session.getPlayheadFrames();

    m_play.setIcon (m_session.isPlaying() ? ui::IconButton::Icon::Pause : ui::IconButton::Icon::Play);

    if (song == nullptr)
        return;

    const TempoMap tempoMap (*song);
    const auto& timeline = tempoMap.getTimeline();

    // Playing or paused: the playhead. Stopped: the edit cursor.
    auto position = m_session.getCursor();
    auto seconds = tempoMap.toSeconds (position);

    if (playhead.has_value())
    {
        position = timeline.framesToBar (*playhead);
        seconds = (double) *playhead / timeline.getSampleRate();
    }

    // Before the left locator while playing: the count-in, counted down to beat 1
    if (const auto leftFrame = timeline.barToFrames (song->getLeftLocator());
        playhead.has_value() && *playhead < leftFrame && song->getMetronomeState() != MetronomeState::Off)
    {
        const auto beatLength = timeline.getBeatUnitLengthInFrames (timeline.barToTicks (song->getLeftLocator()));
        const auto beatsLeft = (int) std::ceil ((double) (leftFrame - *playhead) / juce::jmax (1.0, beatLength));
        m_position.setMessage ("Count-in " + juce::String (juce::jlimit (1, countInBeats, countInBeats + 1 - beatsLeft)));
    }
    else
        m_position.setPosition (position);

    m_time.setText (UIHelpers::timeText (seconds), juce::dontSendNotification);
    m_tempo.setText (juce::String (tempoMap.tempoAt (position)) + " BPM", juce::dontSendNotification);
    m_signature.setText (UIHelpers::signatureText (tempoMap.signatureAt (position.m_bar)), juce::dontSendNotification);
}

//==============================================================================
void TransportBar::PositionReadout::setPosition (const Bar& bar)
{
    if (m_bar == bar)
        return;

    m_bar = bar;
    setText ({}, juce::dontSendNotification);   // the fields are drawn in paint()
    repaint();
}

void TransportBar::PositionReadout::setMessage (const juce::String& message)
{
    m_bar.reset();
    setText (message, juce::dontSendNotification);
}

void TransportBar::PositionReadout::paint (juce::Graphics& g)
{
    ui::Label::paint (g);   // the recessed box (and the message, if any)

    if (! m_bar.has_value())
        return;

    auto& theme = ui::getTheme (*this);
    const auto font = theme.getLabelFont (*this);
    g.setFont (font);
    g.setColour (findColour (juce::Label::textColourId).withMultipliedAlpha (isEnabled() ? 1.0f : 0.5f));

    // Widest values: bar 999, beat 99 (up to 99/x signatures), division 4, tick 239.
    const juce::String fields[] { juce::String (m_bar->m_bar), juce::String (m_bar->m_beat), juce::String (m_bar->m_division), juce::String (m_bar->m_tick) };
    const char* widest[] { "000", "00", "0", "000" };
    const auto gap = juce::GlyphArrangement::getStringWidth (font, " ") * 1.6f;

    float slots[4], total = 3.0f * gap;

    for (int i = 0; i < 4; ++i)
        total += (slots[i] = juce::GlyphArrangement::getStringWidth (font, widest[i]));

    auto x = ((float) getWidth() - total) * 0.5f;

    for (int i = 0; i < 4; ++i)
    {
        g.drawText (fields[i], juce::Rectangle<float> (x, 0.0f, slots[i], (float) getHeight()), juce::Justification::centred, false);
        x += slots[i] + gap;
    }
}
