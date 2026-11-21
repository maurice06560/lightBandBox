#include "TransportBar.h"
#include "PluginTest.h"   // TEMPORARY
#include "../Audio/MBoxAudioEngine.h"

TransportBar::TransportBar (Session& session)
    : m_session (session)
{
    for (auto* c : std::initializer_list<juce::Component*> { &m_browserToggle, &m_editorToggle, &m_play, &m_stop, &m_metronome, &m_tuner, &m_test, &m_songTitle, &m_songCaption,
                                                              &m_position, &m_time, &m_tempo, &m_signature,
                                                              &m_positionCaption, &m_timeCaption, &m_tempoCaption, &m_signatureCaption,
                                                              &m_save, &m_banks, &m_audio, &m_load })
        addAndMakeVisible (c);

    for (auto* caption : { &m_positionCaption, &m_timeCaption, &m_tempoCaption, &m_signatureCaption })
        caption->setJustificationType (juce::Justification::centred);

    m_browserToggle.setTooltip ("Show / hide the playlists (Cmd-1)");
    m_editorToggle.setTooltip ("Show / hide the editor (Cmd-2)");
    m_browserToggle.onClick = [this] { if (onToggleBrowser != nullptr) onToggleBrowser(); };
    m_editorToggle.onClick = [this] { if (onToggleEditor != nullptr) onToggleEditor(); };

    m_play.setTooltip ("Play from the edit cursor / pause / resume (space)");
    m_stop.setTooltip ("Stop (S); the next play starts again from the edit cursor");
    m_metronome.setTooltip ("Metronome on/off for this song");
    m_tuner.setTooltip ("Tuner: the chosen input goes to the tuner only while it is open");
    m_save.setTooltip ("Save every unsaved song and the banks");
    m_banks.setTooltip ("Edit the banks and their effect chains");
    m_audio.setTooltip ("Choose the audio device");
    m_load.setTooltip ("Load another OneBox data folder");

    m_play.onClick = [this] { togglePlayPause(); };
    m_stop.onClick = [this] { stop(); };
    m_metronome.onClick = [this]
    {
        const auto active = m_metronome.getToggleState();

        if (m_session.editMetronome ([active] (MBoxSong& song) { song.setMetronomeActive (active); return juce::Result::ok(); },
                                active ? "Metronome on" : "Metronome off").failed())
            sessionChanged();   // put the button back
    };

    m_tuner.onClick = [this]
    {
        m_tuner.setToggleState (! m_tuner.getToggleState(), juce::dontSendNotification);   // the window sets it

        if (onToggleTuner != nullptr)
            onToggleTuner();
    };
    m_test.onClick  = [this] { PluginTest::run (m_session); };   // TEMPORARY
    m_save.onClick  = [this] { m_session.saveAll(); };
    m_banks.onClick = [this] { if (onShowBanks != nullptr) onShowBanks(); };
    m_audio.onClick = [this] { if (onShowAudioDevice != nullptr) onShowAudioDevice(); };
    m_load.onClick  = [this] { if (onLoadFolder != nullptr) onLoadFolder(); };

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

void TransportBar::setTunerShown (bool shown)
{
    m_tuner.setToggleState (shown, juce::dontSendNotification);
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
    m_session.changed();
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
    for (auto* b : { &m_save, &m_banks, &m_audio, &m_load })
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
    m_tuner.setBounds (area.removeFromRight (52));
    area.removeFromRight (16);
    m_test.setBounds (area.removeFromRight (56));   // TEMPORARY
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
        const auto dirty = m_session.isSongDirty (m_session.getSelectedPlaylistName(), song->getName());
        m_songTitle.setText (song->getName() + (dirty ? " *" : ""), juce::dontSendNotification);
        m_songCaption.setText (m_session.getSelectedPlaylistName() + "  |  " + juce::String ((int) song->getTracks().size())
                                   + " track(s), " + juce::String (song->getTotalBars()) + " bars",
                               juce::dontSendNotification);
        m_metronome.setToggleState (song->isMetronomeActive(), juce::dontSendNotification);
    }
    else
    {
        m_songTitle.setText ("No song selected", juce::dontSendNotification);
        m_songCaption.setText ("Create or select a song in the playlist browser", juce::dontSendNotification);
    }

    for (auto* c : std::initializer_list<juce::Component*> { &m_play, &m_stop, &m_metronome })
        c->setEnabled (song != nullptr);

    m_save.setVariant (m_session.hasUnsavedChanges() ? ui::Button::Variant::Accent : ui::Button::Variant::Standard);
    updateReadouts();
}

void TransportBar::timerCallback()
{
    m_session.pollEngine();

    if (auto* engine = m_session.getApp().getAudioEngine(); engine != nullptr && engine->hasReachedEnd())
        m_session.songReachedEnd();

    updateReadouts();
}

void TransportBar::updateReadouts()
{
    auto* engine = m_session.getApp().getAudioEngine();
    auto* song = m_session.getSelectedSong();
    const auto active = engine != nullptr && engine->isActive();
    const auto playing = m_session.isPlaying();

    m_play.setIcon (playing ? ui::IconButton::Icon::Pause : ui::IconButton::Icon::Play);

    if (song == nullptr)
        return;

    auto position = active ? engine->getPositionAsBar() : m_session.getCursor();

    if (active && engine->isCountingIn())
        m_position.setMessage ("Count-in " + juce::String (engine->getCountdownBeat()));
    else
        m_position.setPosition (position);

    m_time.setText (UIHelpers::timeText (active ? engine->getPositionInSeconds() : TempoMap (*song).toSeconds (position)),
                    juce::dontSendNotification);

    int bpm = song->getTempoGrid().front().second;

    for (const auto& [bar, tempo] : song->getTempoGrid())
        if (! (position < bar))
            bpm = tempo;

    m_tempo.setText (juce::String (bpm) + " BPM", juce::dontSendNotification);
    m_signature.setText (song->getSignatureAt (position.bar).toString(), juce::dontSendNotification);
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
    const juce::String fields[] { juce::String (m_bar->bar), juce::String (m_bar->beat), juce::String (m_bar->division), juce::String (m_bar->tick) };
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
