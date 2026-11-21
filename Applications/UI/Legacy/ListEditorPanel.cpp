#include "ListEditorPanel.h"
#include "../Audio/MBoxAudioEngine.h"
#include "../Audio/TempoMap.h"

namespace
{
    juce::StringArray deviceInputNames (Session& session)
    {
        auto* engine = session.getApp().getAudioEngine();
        return engine != nullptr ? engine->getInputChannelNames() : juce::StringArray();
    }

    /** "Field name" + control rows used by the Song and Track pages. */
    struct FormRow
    {
        std::unique_ptr<ui::Label> caption;
        juce::Component* control;
        int height;
    };

    void layoutForm (std::vector<FormRow>& rows, juce::Rectangle<int> area, int captionWidth)
    {
        for (auto& row : rows)
        {
            auto line = area.removeFromTop (row.height);
            area.removeFromTop (8);

            if (row.caption != nullptr)
                row.caption->setBounds (line.removeFromLeft (captionWidth));

            if (row.control != nullptr)
                row.control->setBounds (line);
        }
    }

    std::unique_ptr<ui::Label> caption (const juce::String& text)
    {
        return std::make_unique<ui::Label> (text, ui::Label::Style::Caption);
    }

    /** A Value label the user edits by double-clicking. */
    void makeEditable (ui::Label& label, std::function<void (const juce::String&)> onCommit)
    {
        label.setEditable (false, true);
        label.setTooltip ("Double-click to edit");
        label.onTextChange = [&label, commit = std::move (onCommit)] { commit (label.getText()); };
    }
}

//==============================================================================
int ListEditorPanel::idForOutputs (MBoxTrack::Outputs outputs)
{
    const auto [first, second] = outputs;

    if (outputs == MBoxMaster::noOutputs)
        return noOutputsId;

    return outputs == MBoxTrack::Outputs { 0, 0 } ? 1 : second == -1 ? 2000 + first : 1000 + first * 100 + second;
}

juce::String ListEditorPanel::outputsText (MBoxTrack::Outputs outputs)
{
    const auto [first, second] = outputs;

    if (outputs == MBoxMaster::noOutputs)
        return "N/A";

    return outputs == MBoxTrack::Outputs { 0, 0 } ? "1/2" : second == -1 ? juce::String (first) + " mono" : juce::String (first) + "/" + juce::String (second);
}

juce::PopupMenu ListEditorPanel::outputsMenu (const juce::StringArray& outputNames, MBoxTrack::Outputs current, bool forMix)
{
    juce::PopupMenu menu;
    const auto count = juce::jmax (2, outputNames.size());
    auto name = [&] (int channel) { return channel <= outputNames.size() ? outputNames[channel - 1] : "Output " + juce::String (channel); };
    const auto currentId = idForOutputs (current);
    auto found = false;

    auto add = [&] (int id, const juce::String& text)
    {
        found = found || id == currentId;
        menu.addItem (id, text, true, id == currentId);
    };

    // A mix can go nowhere; it has no "default" (its outputs are always explicit).
    if (forMix)
        add (noOutputsId, "N/A (not played)");
    else
        add (1, "Default (1/2)");

    menu.addSectionHeader ("Stereo");

    for (int left = 1; left + 1 <= count; left += 2)
        add (1000 + left * 100 + left + 1, juce::String (left) + "/" + juce::String (left + 1) + "   " + name (left) + " + " + name (left + 1));

    menu.addSectionHeader ("Mono");

    for (int channel = 1; channel <= count; ++channel)
        add (2000 + channel, juce::String (channel) + "   " + name (channel));

    // Outputs the current device doesn't have (e.g. saved with another device) stay listed.
    if (! found)
        menu.addItem (currentId, outputsText (current), true, true);

    return menu;
}

MBoxTrack::Outputs ListEditorPanel::outputsForId (int id)
{
    if (id == noOutputsId)
        return MBoxMaster::noOutputs;

    if (id >= 2000)
        return { id - 2000, -1 };

    if (id >= 1000)
        return { (id - 1000) / 100, (id - 1000) % 100 };

    return { 0, 0 };
}

//==============================================================================
/** The selected song's settings, and its metronome. */
class SongPage final : public juce::Component
{
public:
    explicit SongPage (Session& session) : m_session (session)
    {
        makeEditable (m_name, [this] (const juce::String& text)
        {
            if (text.trim() != m_session.getSelectedSongName())
                m_session.renameSong (m_session.getSelectedPlaylistName(), m_session.getSelectedSongName(), text.trim());
            else
                refresh();
        });

        makeEditable (m_startBar, [this] (const juce::String& text)
        {
            const auto bar = text.getIntValue();
            m_session.editSong ([bar] (MBoxSong& s) { return s.setStartBar (bar); }, "Song starts at bar " + juce::String (bar));
        });

        // The bar the END marker stands on in the arrangement: the one after the song's last bar.
        makeEditable (m_endBar, [this] (const juce::String& text)
        {
            const auto bar = text.getIntValue();
            m_session.editSong ([bar] (MBoxSong& s) { return s.setEndBar (bar); }, "Song ends at bar " + juce::String (bar));
        });

        m_metronomeOn.onClick = [this]
        {
            const auto on = m_metronomeOn.getToggleState();
            m_session.editMetronome ([on] (MBoxSong& s) { s.setMetronomeActive (on); return juce::Result::ok(); }, on ? "Metronome on" : "Metronome off");
        };

        m_countdown.addItem ("On", countdownOnId);
        m_countdown.addItem ("Off", countdownOffId);
        m_countdown.addItem ("Countdown only", countdownOnlyId);
        m_countdown.setTooltip ("Countdown only: the " + juce::String (MBoxMetronome::countdownBeats)
                                + "-beat count-in plays, even with the click off, then the click stays silent");
        m_countdown.onChange = [this]
        {
            using Countdown = MBoxMetronome::Countdown;
            const auto id = m_countdown.getSelectedId();
            const auto mode = id == countdownOffId ? Countdown::off : id == countdownOnlyId ? Countdown::only : Countdown::on;
            m_session.editMetronome ([mode] (MBoxSong& s) { s.getMetronome().setCountdown (mode); return juce::Result::ok(); },
                                     mode == Countdown::off ? "Countdown off" : mode == Countdown::only ? "Countdown only" : "Countdown on",
                                     Session::SongChange::yes);
        };

        m_volume.setRange (MBoxMetronome::minVolume, MBoxMetronome::maxVolume, 0.5);
        m_volume.setTextValueSuffix (" dB");
        m_volume.onValueChange = [this]
        {
            const auto value = (float) m_volume.getValue();
            m_session.editMetronome ([value] (MBoxSong& s) { return s.getMetronome().setVolume (value); },
                                "Click volume " + UIHelpers::volumeText (value));
        };

        m_tone.setRange (0.0, 1.0, 0.01);
        m_tone.setNumDecimalPlacesToDisplay (2);
        m_tone.onValueChange = [this]   // live: heard while dragging
        {
            const auto value = (float) m_tone.getValue();
            m_session.editMetronome ([value] (MBoxSong& s) { return s.getMetronome().setTone (value); },
                                "Click tone " + juce::String (value, 2));
        };

        // Any combination of the mixes, live like the rest of the click.
        for (int m = 0; m < MBoxSong::numMixes; ++m)
        {
            auto& button = m_clickMixes[(size_t) m];
            button.setTooltip ("Whether the click plays in this mix");
            button.onClick = [this, m, &button] { m_session.setClickInMix (m, button.getToggleState()); };
            m_clickMixRow.addAndMakeVisible (button);
        }

        m_rows.push_back ({ caption ("Name"), &m_name, 30 });
        m_rows.push_back ({ caption ("Start bar"), &m_startBar, 30 });
        m_rows.push_back ({ caption ("End bar"), &m_endBar, 30 });
        m_rows.push_back ({ caption ("Length"), &m_length, 30 });
        m_rows.push_back ({ nullptr, &m_metronomeHeading, 30 });
        m_rows.push_back ({ caption ("Click"), &m_metronomeOn, 34 });
        m_rows.push_back ({ caption ("Countdown"), &m_countdown, 34 });
        m_rows.push_back ({ caption ("Plays in"), &m_clickMixRow, 30 });

        for (auto& row : m_rows)
        {
            if (row.caption != nullptr)
                addAndMakeVisible (*row.caption);

            addAndMakeVisible (row.control);
        }

        addAndMakeVisible (m_volume);
        addAndMakeVisible (m_tone);
        addAndMakeVisible (m_placeholder);
        refresh();
    }

    void refresh()
    {
        auto* song = m_session.getSelectedSong();
        const auto hasSong = song != nullptr;

        for (int i = 0; i < getNumChildComponents(); ++i)
            getChildComponent (i)->setVisible (hasSong);

        m_placeholder.setVisible (! hasSong);

        if (! hasSong)
            return;

        m_name.setText (song->getName(), juce::dontSendNotification);
        m_startBar.setText (juce::String (song->getStartBar()), juce::dontSendNotification);
        m_endBar.setText (juce::String (song->getEndBar()), juce::dontSendNotification);

        // The metronome stays editable while playing or paused; the song's structure doesn't.
        const auto unlocked = ! m_session.isEditLocked();
        m_name.setEnabled (unlocked);
        m_startBar.setEnabled (unlocked);
        m_endBar.setEnabled (unlocked);

        // What plays: from the start bar to the end.
        const TempoMap tempoMap (*song);
        const auto startSeconds = tempoMap.toSeconds (Bar { song->getStartBar(), 1, 1, 0 });
        m_length.setText (UIHelpers::timeText (tempoMap.getLengthInSeconds() - startSeconds), juce::dontSendNotification);

        const auto& metronome = song->getMetronome();
        m_metronomeOn.setToggleState (song->isMetronomeActive(), juce::dontSendNotification);
        m_metronomeOn.setButtonText (song->isMetronomeActive() ? "On" : "Off");
        const auto countdown = metronome.getCountdown();
        m_countdown.setSelectedId (countdown == MBoxMetronome::Countdown::off ? countdownOffId
                                 : countdown == MBoxMetronome::Countdown::only ? countdownOnlyId : countdownOnId,
                                   juce::dontSendNotification);

        if (! m_volume.isMouseButtonDown())
            m_volume.setValue (metronome.getVolume(), juce::dontSendNotification);

        if (! m_tone.isMouseButtonDown())
            m_tone.setValue (metronome.getTone(), juce::dontSendNotification);

        for (int m = 0; m < MBoxSong::numMixes; ++m)
        {
            auto& button = m_clickMixes[(size_t) m];
            button.setButtonText (song->getMix (m).getName());
            button.setToggleState (metronome.playsIn (m), juce::dontSendNotification);
        }
    }

    void resized() override
    {
        auto area = getLocalBounds();
        m_placeholder.setBounds (area.withHeight (60));

        auto form = area.removeFromTop (7 * 38 + 34);
        layoutForm (m_rows, form, 90);

        auto mixes = m_clickMixRow.getLocalBounds();
        const auto width = mixes.getWidth() / MBoxSong::numMixes;

        for (auto& button : m_clickMixes)
            button.setBounds (mixes.removeFromLeft (width).reduced (2, 0));

        area.removeFromTop (8);
        auto knobs = area.removeFromTop (juce::jmin (130, area.getHeight()));
        m_volume.setBounds (knobs.removeFromLeft (knobs.getWidth() / 2).reduced (8, 0));
        m_tone.setBounds (knobs.reduced (8, 0));
    }

private:
    Session& m_session;
    std::vector<FormRow> m_rows;

    ui::Label m_name { {}, ui::Label::Style::Value };
    static constexpr int countdownOnId = 1, countdownOffId = 2, countdownOnlyId = 3;

    ui::Label m_startBar { {}, ui::Label::Style::Value };
    ui::Label m_endBar { {}, ui::Label::Style::Value };
    ui::Label m_length { {}, ui::Label::Style::Body };
    ui::Label m_metronomeHeading { "Metronome", ui::Label::Style::Heading };
    ui::Button m_metronomeOn { "On", ui::Button::Variant::Standard, true };
    juce::ComboBox m_countdown;
    juce::Component m_clickMixRow;
    std::array<ui::Button, MBoxSong::numMixes> m_clickMixes { ui::Button { "Mix 1", ui::Button::Variant::Standard, true },
                                                              ui::Button { "Mix 2", ui::Button::Variant::Standard, true },
                                                              ui::Button { "Mix 3", ui::Button::Variant::Standard, true } };
    ui::Knob m_volume { "Volume" };
    ui::Knob m_tone { "Tone" };
    ui::Label m_placeholder { "No song selected", ui::Label::Style::Caption };
};

//==============================================================================
/** The selected track: type, input, WAV, and its volume and mute / solo in the selected mix. */
class TrackPage final : public juce::Component
{
public:
    explicit TrackPage (Session& session) : m_session (session)
    {
        m_type.onChange = [this] (int index)
        {
            const auto type = index == 1 ? TrackType::Wav : TrackType::Physical;
            m_session.editTrack ([type] (MBoxTrack& t) { t.setTrackType (type); return juce::Result::ok(); },
                                 "Track type: " + toString (type));
        };

        m_input.onChange = [this]
        {
            const auto channel = m_input.getSelectedId();
            m_session.editTrack ([channel] (MBoxTrack& t) { return t.setPhysicalInput (channel); }, "Input " + juce::String (channel));
        };

        makeEditable (m_start, [this] (const juce::String& text)
        {
            auto* song = m_session.getSelectedSong();
            const auto position = UIHelpers::parsePosition (text);

            if (song == nullptr || ! position.has_value())
            {
                m_session.setStatus ("Invalid position \"" + text + "\" (e.g. 5 or 5 2 1 0)", true);
                refresh();
                return;
            }

            if (auto r = song->validatePosition (*position); r.failed())
            {
                m_session.report (r);
                refresh();
                return;
            }

            m_session.editTrack ([&] (MBoxTrack& t)
                                 {
                                     if (! t.hasFile())
                                         return juce::Result::fail ("Choose a WAV first");

                                     t.setFile (*position, t.getFile().second);
                                     return juce::Result::ok();
                                 },
                                 "WAV starts at " + UIHelpers::positionText (*position));
        });

        makeEditable (m_name, [this] (const juce::String& text)
        {
            m_session.editTrack ([&text] (MBoxTrack& t) { return t.setName (text.toStdString()); }, "Track renamed to " + text.trim());
        });

        makeEditable (m_volume, [this] (const juce::String& text)
        {
            const auto silent = text.containsIgnoreCase ("inf");
            const auto value = silent ? MBoxTrack::silent : text.upToFirstOccurrenceOf ("dB", false, true).getFloatValue();
            m_session.setTrackVolume (m_session.getSelectedTrackId(), value);
        });

        m_choose.onClick = [this] { chooseWav(); };

        // Live, like volume: never greyed by playback.
        m_mute.onClick = [this] { m_session.setTrackMute (m_session.getSelectedTrackId(), m_mute.getToggleState()); };
        m_solo.onClick = [this] { m_session.setTrackSolo (m_session.getSelectedTrackId(), m_solo.getToggleState()); };
        m_mute.setTooltip ("Mute this track");
        m_solo.setTooltip ("Solo: only soloed tracks play");
        m_muteSolo.addAndMakeVisible (m_mute);
        m_muteSolo.addAndMakeVisible (m_solo);

        // Live too: whether the input is heard while the song doesn't play.
        m_monitor.onClick = [this] { m_session.setTrackMonitored (m_session.getSelectedTrackId(), m_monitor.getToggleState()); };
        m_diSend.setTooltip ("Sends this track's own sound (after its bankslot effects; a WAV as it is) straight to outputs, "
                             "before any mix. Outputs a submix uses can't be picked.");
        m_diSend.onChange = [this]
        {
            const auto id = m_diSend.getSelectedId();

            if (id == 0)
                return;

            const auto outputs = ListEditorPanel::outputsForId (id);
            const auto trackId = m_session.getSelectedTrackId();

            // Deferred: the edit refreshes this page, which refills the box.
            juce::MessageManager::callAsync ([&target = m_session, trackId, outputs]
            {
                target.setTrackDiSend (trackId, outputs, ListEditorPanel::outputsText (outputs));
            });
        };
        m_monitor.setTooltip ("Hear this input while the song is stopped or paused, through the bankslot at the edit cursor. "
                              "During playback it is always heard.");
        m_clear.onClick = [this]
        {
            m_session.editTrack ([] (MBoxTrack& t) { t.clearFile(); return juce::Result::ok(); }, "WAV removed from the track");
        };

        m_rows.push_back ({ nullptr, &m_title, 30 });
        m_rows.push_back ({ caption ("Name"), &m_name, 30 });
        m_rows.push_back ({ caption ("Type"), &m_type, 32 });
        m_rows.push_back ({ caption ("Input"), &m_input, 28 });
        m_rows.push_back ({ caption ("Monitor"), &m_monitor, 30 });
        m_rows.push_back ({ caption ("DI send"), &m_diSend, 28 });
        m_rows.push_back ({ caption ("WAV"), &m_file, 30 });
        m_rows.push_back ({ nullptr, &m_wavButtons, 32 });
        m_rows.push_back ({ caption ("Starts at"), &m_start, 30 });
        m_rows.push_back ({ caption ("Mix"), &m_mix, 30 });
        m_rows.push_back ({ caption ("Volume"), &m_volume, 30 });
        m_rows.push_back ({ caption ("Mute / Solo"), &m_muteSolo, 30 });

        for (auto& row : m_rows)
        {
            if (row.caption != nullptr)
                addAndMakeVisible (*row.caption);

            addAndMakeVisible (row.control);
        }

        m_wavButtons.addAndMakeVisible (m_choose);
        m_wavButtons.addAndMakeVisible (m_clear);
        addAndMakeVisible (m_placeholder);
        addAndMakeVisible (m_hint);
        refresh();
    }

    void refresh()
    {
        auto* track = m_session.getSelectedTrack();

        for (int i = 0; i < getNumChildComponents(); ++i)
            getChildComponent (i)->setVisible (track != nullptr);

        m_placeholder.setVisible (track == nullptr);

        if (track == nullptr)
            return;

        const auto isWav = track->getTrackType() == TrackType::Wav;
        m_title.setText ("Track " + juce::String (track->getId()), juce::dontSendNotification);
        m_name.setText (juce::String (track->getName()), juce::dontSendNotification);
        m_type.setSelectedIndex (isWav ? 1 : 0, juce::dontSendNotification);

        // Only the volume changes live; the rest of the track waits for playback to stop.
        const auto unlocked = ! m_session.isEditLocked();
        m_name.setEnabled (unlocked);
        m_type.setEnabled (unlocked);

        // Input: the device's input channels (or 1..8 when no device is open).
        m_input.clear (juce::dontSendNotification);
        const auto inputs = deviceInputNames (m_session);

        for (int channel = 1; channel <= juce::jmax (8, inputs.size()); ++channel)
            m_input.addItem (juce::String (channel) + "   " + (channel <= inputs.size() ? inputs[channel - 1] : juce::String ("Input ") + juce::String (channel)),
                             channel);

        m_input.setSelectedId (juce::jmax (1, track->getPhysicalInput()), juce::dontSendNotification);
        m_input.setEnabled (! isWav && unlocked);

        m_monitor.setToggleState (track->isMonitored(), juce::dontSendNotification);
        m_monitor.setButtonText (isWav ? "-" : track->isMonitored() ? "Also when stopped" : "Only while playing");
        m_monitor.setEnabled (! isWav);

        fillDiSend (*m_session.getSelectedSong(), *track);
        m_diSend.setEnabled (unlocked);

        m_file.setText (track->hasFile() ? juce::File::createFileWithoutCheckingPath (track->getFile().second).getFileName()
                                         : juce::String ("None"),
                        juce::dontSendNotification);
        m_start.setText (track->hasFile() ? UIHelpers::positionText (track->getFile().first) : juce::String ("-"), juce::dontSendNotification);
        m_choose.setEnabled (isWav && unlocked);
        m_clear.setEnabled (isWav && track->hasFile() && unlocked);
        m_start.setEnabled (isWav && track->hasFile() && unlocked);
        m_file.setEnabled (isWav);

        // Volume and mute / solo belong to the mix shown in the mixer.
        const auto& mix = *m_session.getSelectedMix();
        const auto& channel = mix.getChannel (track->getId());
        m_mix.setText (mix.getName(), juce::dontSendNotification);
        m_volume.setText (UIHelpers::volumeText (channel.getVolume()), juce::dontSendNotification);
        m_mute.setToggleState (channel.isMuted(), juce::dontSendNotification);
        m_solo.setToggleState (channel.isSoloed(), juce::dontSendNotification);

        m_hint.setText (isWav ? "The WAV starts at its bar and plays to its end; the song grows if needed."
                              : "Plays the input live through the bankslots set in the Bankslots list.",
                        juce::dontSendNotification);
    }

    void resized() override
    {
        auto area = getLocalBounds();
        m_placeholder.setBounds (area.withHeight (60));
        layoutForm (m_rows, area.removeFromTop (12 * 38), 90);

        auto muteSolo = m_muteSolo.getLocalBounds();
        m_mute.setBounds (muteSolo.removeFromLeft (muteSolo.getWidth() / 2).reduced (2, 0));
        m_solo.setBounds (muteSolo.reduced (2, 0));
        m_hint.setBounds (area.removeFromTop (44));

        auto buttons = m_wavButtons.getLocalBounds().withTrimmedLeft (90);
        m_choose.setBounds (buttons.removeFromLeft (buttons.getWidth() / 2).reduced (2, 0));
        m_clear.setBounds (buttons.reduced (2, 0));
    }

private:
    void chooseWav()
    {
        auto* track = m_session.getSelectedTrack();

        if (track == nullptr || ! m_session.canEdit())
            return;

        const auto trackId = track->getId();
        const auto start = track->hasFile() ? track->getFile().first : m_session.getCursor();

        m_chooser = std::make_unique<juce::FileChooser> ("Choose a WAV (PCM 16-bit, 44.1 kHz)",
                                                         juce::File::getSpecialLocation (juce::File::userMusicDirectory), "*.wav");
        m_chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                                [this, trackId, start] (const juce::FileChooser& chooser)
                                {
                                    if (const auto file = chooser.getResult(); file.existsAsFile())
                                        m_session.importWav (trackId, start, file);
                                });
    }

    Session& m_session;
    std::vector<FormRow> m_rows;

    ui::Label m_title { {}, ui::Label::Style::Heading };
    ui::Label m_name { {}, ui::Label::Style::Value };
    ui::SegmentedControl m_type { { "Physical", "WAV" } };
    juce::ComboBox m_input;
    ui::Label m_mix { {}, ui::Label::Style::Body };
    ui::Label m_file { {}, ui::Label::Style::Body };
    juce::Component m_wavButtons;
    ui::Button m_choose { "Choose..." };
    ui::Button m_clear { "Clear" };
    ui::Label m_start { {}, ui::Label::Style::Value };
    ui::Label m_volume { {}, ui::Label::Style::Value };
    ui::Button m_monitor { "Only while playing", ui::Button::Variant::Standard, true };
    juce::ComboBox m_diSend;

    /** None, then the device's stereo pairs and monos; those already in use in the song (by a mix, or by
        another track's DI send) are listed but greyed, with what uses them.
    */
    void fillDiSend (const MBoxSong& song, const MBoxTrack& track)
    {
        auto* engine = m_session.getApp().getAudioEngine();
        const auto names = engine != nullptr ? engine->getOutputChannelNames() : juce::StringArray();
        const auto count = juce::jmax (2, names.size());
        auto name = [&] (int channel) { return channel <= names.size() ? names[channel - 1] : "Output " + juce::String (channel); };
        const auto current = track.getDiSend();
        const auto currentId = ListEditorPanel::idForOutputs (current);
        auto found = false;
        juce::PopupMenu menu;

        auto add = [&] (MBoxTrack::Outputs outputs, const juce::String& text)
        {
            const auto id = ListEditorPanel::idForOutputs (outputs);
            const auto mix = outputs == MBoxTrack::noDiSend ? -1 : song.mixUsing (outputs);
            const auto* other = outputs == MBoxTrack::noDiSend ? nullptr : song.diSendUsing (outputs, track.getId());
            const auto user = mix >= 0 ? song.getMix (mix).getName()
                            : other != nullptr ? juce::String (other->getName()) + " DI" : juce::String();
            found = found || id == currentId;
            menu.addItem (id, text + (user.isNotEmpty() ? "   (" + user + ")" : juce::String()), user.isEmpty(), id == currentId);
        };

        add (MBoxTrack::noDiSend, "None");
        menu.addSectionHeader ("Stereo");

        for (int left = 1; left + 1 <= count; left += 2)
            add ({ left, left + 1 }, juce::String (left) + "/" + juce::String (left + 1) + "   " + name (left) + " + " + name (left + 1));

        menu.addSectionHeader ("Mono");

        for (int channel = 1; channel <= count; ++channel)
            add ({ channel, -1 }, juce::String (channel) + "   " + name (channel));

        // A DI send on outputs the current device doesn't have stays listed.
        if (! found)
            menu.addItem (currentId, ListEditorPanel::outputsText (current), true, true);

        m_diSend.clear (juce::dontSendNotification);
        *m_diSend.getRootMenu() = menu;
        m_diSend.setSelectedId (currentId, juce::dontSendNotification);

        if (current == MBoxTrack::noDiSend)
            m_diSend.setText ("None", juce::dontSendNotification);
    }
    juce::Component m_muteSolo;
    ui::Button m_mute { "Mute", ui::Button::Variant::Mute, true };
    ui::Button m_solo { "Solo", ui::Button::Variant::Solo, true };
    ui::Label m_hint { {}, ui::Label::Style::Caption };
    ui::Label m_placeholder { "No track selected", ui::Label::Style::Caption };
    std::unique_ptr<juce::FileChooser> m_chooser;
};

//==============================================================================
/** A Position | value | ... list with + and - buttons, editable in place (double-click a cell). */
class EventTable : public juce::Component,
                   public juce::TableListBoxModel
{
public:
    struct Column
    {
        juce::String name;
        int width;
        bool editable;
    };

    EventTable (Session& session, std::vector<Column> columns) : m_session (session), m_columns (std::move (columns))
    {
        for (size_t i = 0; i < m_columns.size(); ++i)
            m_table.getHeader().addColumn (m_columns[i].name, (int) i + 1, m_columns[i].width, 30, -1,
                                           juce::TableHeaderComponent::notSortable | (i + 1 == m_columns.size() ? juce::TableHeaderComponent::defaultFlags : 0));

        m_table.getHeader().setStretchToFitActive (true);
        m_table.setHeaderHeight (28);
        m_table.setRowHeight (28);
        m_table.setColour (juce::ListBox::backgroundColourId, ui::LookAndFeel::getShared().getPalette().arcTrack.withAlpha (0.6f));

        m_add.onClick = [this] { m_session.report (addRow()); };
        m_remove.onClick = [this]
        {
            const auto row = m_table.getSelectedRow();

            if (row < 0)
                m_session.setStatus ("Select a row to delete", true);
            else
                m_session.report (deleteRow (row));
        };

        m_add.setTooltip ("Add an entry at the edit cursor");
        m_remove.setTooltip ("Delete the selected entry");

        for (auto* c : std::initializer_list<juce::Component*> { &m_add, &m_remove, &m_caption, &m_table, &m_hint })
            addAndMakeVisible (c);
    }

    ~EventTable() override
    {
        // By now the derived table is gone, so the list must not call back into getNumRows() & co.
        m_table.setModel (nullptr);
    }

    void refresh()
    {
        // The model is attached here rather than in the constructor: the table queries it straight away,
        // and the row methods are pure virtual until the derived class is fully constructed.
        if (m_table.getTableListBoxModel() == nullptr)
            m_table.setModel (this);

        m_table.updateContent();
        m_table.repaint();
        m_caption.setText (getCaption(), juce::dontSendNotification);
        m_hint.setText (getHint(), juce::dontSendNotification);
        m_add.setEnabled (canAdd() && ! m_session.isEditLocked());
        // Same rule as adding (no song, or a WAV track for the bankslots: nothing to edit), and something to delete.
        m_remove.setEnabled (canAdd() && getNumRows() > 0 && ! m_session.isEditLocked());
    }

    void selectRow (int row)
    {
        if (row >= 0)
        {
            m_table.selectRow (row);
            m_table.scrollToEnsureRowIsOnscreen (row);
        }
    }

    virtual int rowForPosition (const Bar&) = 0;

    void resized() override
    {
        auto area = getLocalBounds();
        auto toolbar = area.removeFromTop (34);
        m_add.setBounds (toolbar.removeFromLeft (40));
        toolbar.removeFromLeft (4);
        m_remove.setBounds (toolbar.removeFromLeft (40));
        toolbar.removeFromLeft (10);
        m_caption.setBounds (toolbar);
        area.removeFromTop (8);
        m_hint.setBounds (area.removeFromBottom (40));
        m_table.setBounds (area);
    }

    //==============================================================================
    void paintRowBackground (juce::Graphics& g, int row, int width, int height, bool selected) override
    {
        const auto& palette = ui::LookAndFeel::getShared().getPalette();
        g.setColour (row % 2 == 0 ? palette.panelFill.brighter (0.03f) : palette.panelFill.darker (0.1f));
        g.fillRect (0, 0, width, height);

        if (selected)
        {
            g.setColour (palette.accent.withAlpha (0.35f));
            g.fillRect (0, 0, width, height);
            g.setColour (palette.accent);
            g.fillRect (0, 0, 3, height);
        }
    }

    void paintCell (juce::Graphics& g, int row, int columnId, int width, int height, bool) override
    {
        if (isCellEditable (row, columnId) || hasCustomCell (columnId))
            return;   // a component draws it

        const auto& palette = ui::LookAndFeel::getShared().getPalette();
        g.setColour (columnId == 1 && row == 0 ? palette.textDim.brighter (0.3f) : palette.text);
        g.setFont (ui::LookAndFeel::makeFont (13.0f));
        g.drawText (getCellText (row, columnId), juce::Rectangle<int> (width, height).reduced (8, 0),
                    columnId == 1 ? juce::Justification::centredRight : juce::Justification::centredLeft, true);
    }

    juce::Component* refreshComponentForCell (int row, int columnId, bool, juce::Component* existing) override
    {
        if (hasCustomCell (columnId))
            return createCustomCell (row, columnId, existing);

        if (! isCellEditable (row, columnId))
        {
            delete existing;
            return nullptr;
        }

        auto* cell = dynamic_cast<EditableCell*> (existing);

        if (cell == nullptr)
        {
            delete existing;
            cell = new EditableCell (*this);
        }

        cell->setCell (row, columnId, getCellText (row, columnId));
        cell->setEnabled (! m_session.isEditLocked());
        return cell;
    }

    void cellClicked (int row, int, const juce::MouseEvent&) override
    {
        m_table.selectRow (row);
    }

    void deleteKeyPressed (int row) override
    {
        m_session.report (deleteRow (row));
    }

    void commitCell (int row, int columnId, const juce::String& text)
    {
        if (row >= getNumRows())
            return;

        if (const auto result = setCellText (row, columnId, text); result.failed())
        {
            m_session.report (result);
            refresh();
        }
    }

protected:
    virtual juce::String getCaption() = 0;
    virtual juce::String getHint() = 0;
    virtual bool canAdd() = 0;
    virtual juce::String getCellText (int row, int columnId) = 0;
    virtual bool isCellEditable (int row, int columnId) = 0;
    virtual juce::Result setCellText (int row, int columnId, const juce::String& text) = 0;
    virtual juce::Result addRow() = 0;
    virtual juce::Result deleteRow (int row) = 0;
    virtual bool hasCustomCell (int) { return false; }
    virtual juce::Component* createCustomCell (int, int, juce::Component* existing) { delete existing; return nullptr; }

    Session& m_session;
    juce::TableListBox m_table;

private:
    /** A table cell edited in place by double-clicking. */
    class EditableCell final : public juce::Label
    {
    public:
        explicit EditableCell (EventTable& owner) : m_owner (owner)
        {
            setEditable (false, true);
            setJustificationType (juce::Justification::centredLeft);
            setColour (juce::Label::textColourId, ui::LookAndFeel::getShared().getPalette().text.brighter (0.2f));
            setFont (ui::LookAndFeel::makeFont (13.0f));
            setBorderSize ({ 1, 8, 1, 8 });
            // Committing refreshes the table, which may rebuild this very cell, so it runs after the callback returns.
            onTextChange = [this]
            {
                juce::MessageManager::callAsync ([table = juce::Component::SafePointer<EventTable> (&m_owner),
                                                  row = m_row, column = m_column, text = getText()]
                {
                    if (table != nullptr)
                        table->commitCell (row, column, text);
                });
            };
        }

        void setCell (int row, int column, const juce::String& text)
        {
            m_row = row;
            m_column = column;
            setJustificationType (column == 1 ? juce::Justification::centredRight : juce::Justification::centredLeft);
            setText (text, juce::dontSendNotification);
        }

        void mouseDown (const juce::MouseEvent& e) override
        {
            m_owner.m_table.selectRow (m_row);
            juce::Label::mouseDown (e);
        }

    private:
        EventTable& m_owner;
        int m_row = 0, m_column = 0;
    };

    std::vector<Column> m_columns;
    ui::IconButton m_add { ui::IconButton::Icon::Plus };
    ui::IconButton m_remove { ui::IconButton::Icon::Minus };
    ui::Label m_caption { {}, ui::Label::Style::Heading };
    ui::Label m_hint { {}, ui::Label::Style::Caption };
};

//==============================================================================
class TempoTable final : public EventTable
{
public:
    explicit TempoTable (Session& session)
        : EventTable (session, { { "Position", 110, true }, { "Tempo", 90, true }, { "Time", 90, false } })
    {
    }

    int getNumRows() override
    {
        auto* song = m_session.getSelectedSong();
        return song != nullptr ? (int) song->getTempoGrid().size() : 0;
    }

    int rowForPosition (const Bar& position) override
    {
        auto* song = m_session.getSelectedSong();

        if (song == nullptr)
            return -1;

        const auto& grid = song->getTempoGrid();

        for (size_t i = 0; i < grid.size(); ++i)
            if (grid[i].first == position)
                return (int) i;

        return -1;
    }

protected:
    juce::String getCaption() override { return "Tempo changes"; }
    juce::String getHint() override    { return "Double-click a cell to edit. Positions are bar beat division tick. The first tempo can be changed, not deleted."; }
    bool canAdd() override             { return m_session.getSelectedSong() != nullptr; }

    juce::String getCellText (int row, int columnId) override
    {
        auto* song = m_session.getSelectedSong();
        const auto& [bar, bpm] = song->getTempoGrid()[(size_t) row];

        switch (columnId)
        {
            case 1:  return UIHelpers::positionText (bar);
            case 2:  return juce::String (bpm) + " BPM";
            default: return UIHelpers::timeText (TempoMap (*song).toSeconds (bar));
        }
    }

    bool isCellEditable (int row, int columnId) override { return columnId == 2 || (columnId == 1 && row > 0); }

    juce::Result setCellText (int row, int columnId, const juce::String& text) override
    {
        auto* song = m_session.getSelectedSong();
        const auto [bar, bpm] = song->getTempoGrid()[(size_t) row];

        if (columnId == 2)
        {
            const auto value = text.getIntValue();
            return m_session.editSong ([&] (MBoxSong& s) { return s.addTempo ({ bar, value }); }, juce::String (value) + " BPM at " + UIHelpers::positionText (bar));
        }

        const auto position = UIHelpers::parsePosition (text);

        if (! position.has_value())
            return juce::Result::fail ("Invalid position \"" + text + "\" (e.g. 9 or 9 2 1 0)");

        return m_session.editSong ([&] (MBoxSong& s)
                                   {
                                       s.removeTempo (bar);

                                       if (auto r = s.addTempo ({ *position, bpm }); r.failed())
                                       {
                                           s.addTempo ({ bar, bpm });
                                           return r;
                                       }

                                       return juce::Result::ok();
                                   },
                                   "Tempo moved to " + UIHelpers::positionText (*position));
    }

    juce::Result addRow() override
    {
        auto* song = m_session.getSelectedSong();
        const auto position = m_session.getCursor();

        if (rowForPosition (position) >= 0)
            return juce::Result::fail ("There is already a tempo at " + UIHelpers::positionText (position) + ": move the edit cursor first");

        int bpm = song->getTempoGrid().front().second;

        for (const auto& [bar, tempo] : song->getTempoGrid())
            if (! (position < bar))
                bpm = tempo;

        const auto result = m_session.editSong ([&] (MBoxSong& s) { return s.addTempo ({ position, bpm }); },
                                                "Tempo added at " + UIHelpers::positionText (position));
        selectRow (rowForPosition (position));
        return result;
    }

    juce::Result deleteRow (int row) override
    {
        auto* song = m_session.getSelectedSong();
        const auto bar = song->getTempoGrid()[(size_t) row].first;
        return m_session.editSong ([&] (MBoxSong& s) { return s.removeTempo (bar); }, "Tempo at " + UIHelpers::positionText (bar) + " deleted");
    }
};

//==============================================================================
class SignatureTable final : public EventTable
{
public:
    explicit SignatureTable (Session& session)
        : EventTable (session, { { "Bar", 80, true }, { "Signature", 110, true }, { "Beats", 80, false } })
    {
    }

    int getNumRows() override
    {
        auto* song = m_session.getSelectedSong();
        return song != nullptr ? (int) song->getSignatureGrid().size() : 0;
    }

    int rowForPosition (const Bar& position) override
    {
        auto* song = m_session.getSelectedSong();

        if (song == nullptr)
            return -1;

        const auto& grid = song->getSignatureGrid();

        for (size_t i = 0; i < grid.size(); ++i)
            if (grid[i].first.bar == position.bar)
                return (int) i;

        return -1;
    }

protected:
    juce::String getCaption() override { return "Time signatures"; }
    juce::String getHint() override    { return "Signatures change at the start of a bar. Type values like 7/8. The first one can be changed, not deleted."; }
    bool canAdd() override             { return m_session.getSelectedSong() != nullptr; }

    juce::String getCellText (int row, int columnId) override
    {
        const auto& [bar, signature] = m_session.getSelectedSong()->getSignatureGrid()[(size_t) row];

        switch (columnId)
        {
            case 1:  return juce::String (bar.bar);
            case 2:  return signature.toString();
            default: return juce::String (signature.numerator) + " per bar";
        }
    }

    bool isCellEditable (int row, int columnId) override { return columnId == 2 || (columnId == 1 && row > 0); }

    juce::Result setCellText (int row, int columnId, const juce::String& text) override
    {
        const auto [bar, signature] = m_session.getSelectedSong()->getSignatureGrid()[(size_t) row];

        if (columnId == 2)
        {
            const auto parsed = Signature::parse (text);

            if (! parsed.has_value())
                return juce::Result::fail ("Invalid signature \"" + text + "\": numerator 1-32, denominator 1, 2, 4, 8, 16 or 32");

            return m_session.editSong ([&] (MBoxSong& s) { return s.addSignature ({ bar, *parsed }); },
                                       parsed->toString() + " from bar " + juce::String (bar.bar));
        }

        const auto newBar = text.trim().getIntValue();

        if (newBar < 2)
            return juce::Result::fail ("Signature changes go at bar 2 or later (bar 1 always has one)");

        return m_session.editSong ([&] (MBoxSong& s)
                                   {
                                       s.removeSignature (bar);

                                       if (auto r = s.addSignature ({ Bar { newBar, 1, 1, 0 }, signature }); r.failed())
                                       {
                                           s.addSignature ({ bar, signature });
                                           return r;
                                       }

                                       return juce::Result::ok();
                                   },
                                   "Signature moved to bar " + juce::String (newBar));
    }

    juce::Result addRow() override
    {
        auto* song = m_session.getSelectedSong();
        const auto position = Bar { m_session.getCursor().bar, 1, 1, 0 };

        if (rowForPosition (position) >= 0)
            return juce::Result::fail ("Bar " + juce::String (position.bar) + " already has a signature: move the edit cursor first");

        const auto result = m_session.editSong ([&] (MBoxSong& s) { return s.addSignature ({ position, s.getSignatureAt (position.bar) }); },
                                                "Signature added at bar " + juce::String (position.bar) + ": double-click it to change it");
        selectRow (rowForPosition (position));
        juce::ignoreUnused (song);
        return result;
    }

    juce::Result deleteRow (int row) override
    {
        const auto bar = m_session.getSelectedSong()->getSignatureGrid()[(size_t) row].first;
        return m_session.editSong ([&] (MBoxSong& s) { return s.removeSignature (bar); }, "Signature at bar " + juce::String (bar.bar) + " deleted");
    }
};

//==============================================================================
class BankslotTable final : public EventTable
{
public:
    explicit BankslotTable (Session& session)
        : EventTable (session, { { "Position", 100, true }, { "Bank / Slot", 150, false }, { "Effects", 120, false } })
    {
    }

    int getNumRows() override
    {
        auto* track = physicalTrack();
        return track != nullptr ? (int) track->getBankSlots().size() : 0;
    }

    int rowForPosition (const Bar& position) override
    {
        auto* track = physicalTrack();

        if (track == nullptr)
            return -1;

        const auto& grid = track->getBankSlots();

        for (size_t i = 0; i < grid.size(); ++i)
            if (grid[i].first == position)
                return (int) i;

        return -1;
    }

protected:
    MBoxTrack* physicalTrack()
    {
        auto* track = m_session.getSelectedTrack();
        return track != nullptr && track->getTrackType() == TrackType::Physical ? track : nullptr;
    }

    juce::String getCaption() override
    {
        auto* track = m_session.getSelectedTrack();

        if (track == nullptr)
            return "No track selected";

        return juce::String (track->getName()) + (physicalTrack() != nullptr ? " bankslots" : " is a WAV track");
    }

    juce::String getHint() override
    {
        return physicalTrack() != nullptr ? "From each position the track's input plays through that bankslot's effects, until the next switch."
                                          : "Bankslots apply to Physical tracks: select one in the song editor.";
    }

    bool canAdd() override { return physicalTrack() != nullptr; }

    bool hasCustomCell (int columnId) override { return columnId == 2; }

    juce::Component* createCustomCell (int row, int, juce::Component* existing) override
    {
        auto* box = dynamic_cast<juce::ComboBox*> (existing);

        if (box == nullptr)
        {
            delete existing;
            box = new juce::ComboBox();
        }

        box->clear (juce::dontSendNotification);
        const auto& banks = m_session.getApp().getBanks();

        for (int b = 0; b < MBoxApplication::numBanks; ++b)
        {
            box->addSectionHeading (banks[(size_t) b].m_name);

            for (int s = 0; s < MBoxBank::numSlots; ++s)
                box->addItem (UIHelpers::bankSlotName (banks, { b, s }), b * MBoxBank::numSlots + s + 1);
        }

        const auto& ref = physicalTrack()->getBankSlots()[(size_t) row].second;
        box->setSelectedId (ref.bank * MBoxBank::numSlots + ref.slot + 1, juce::dontSendNotification);
        box->setEnabled (! m_session.isEditLocked());
        box->setColour (juce::ComboBox::backgroundColourId, UIHelpers::bankColour (ref.bank).withAlpha (0.35f).darker (0.4f));
        box->onChange = [this, row, box]
        {
            // As with text cells: apply after the callback, since the edit rebuilds this combo box.
            juce::MessageManager::callAsync ([table = juce::Component::SafePointer<BankslotTable> (this), row, id = box->getSelectedId() - 1]
            {
                if (table != nullptr)
                    table->setBankslot (row, { id / MBoxBank::numSlots, id % MBoxBank::numSlots });
            });
        };

        return box;
    }

    void setBankslot (int row, BankSlotRef newRef)
    {
        auto* track = physicalTrack();

        if (track == nullptr || row >= (int) track->getBankSlots().size())
            return;

        const auto position = track->getBankSlots()[(size_t) row].first;

        m_session.editTrack ([&] (MBoxTrack& t)
                             {
                                 auto grid = t.getBankSlots();
                                 grid[(size_t) row].second = newRef;
                                 return t.setBankSlots (std::move (grid), MBoxApplication::numBanks, MBoxBank::numSlots);
                             },
                             UIHelpers::bankSlotName (m_session.getApp().getBanks(), newRef) + " from " + UIHelpers::positionText (position));
    }

    juce::String getCellText (int row, int columnId) override
    {
        const auto& [bar, ref] = physicalTrack()->getBankSlots()[(size_t) row];

        if (columnId == 1)
            return UIHelpers::positionText (bar);

        juce::StringArray effects;

        for (const auto& instance : m_session.getApp().getBanks()[(size_t) ref.bank].m_bankSlots[(size_t) ref.slot].m_effectChain)
            effects.add (instance.getEffect().getName());

        return effects.isEmpty() ? juce::String ("dry") : effects.joinIntoString (", ");
    }

    bool isCellEditable (int, int columnId) override { return columnId == 1; }

    juce::Result setCellText (int row, int, const juce::String& text) override
    {
        const auto position = UIHelpers::parsePosition (text);

        if (! position.has_value())
            return juce::Result::fail ("Invalid position \"" + text + "\" (e.g. 12 or 12 3 1 0)");

        if (auto r = m_session.getSelectedSong()->validatePosition (*position); r.failed())
            return r;

        return m_session.editTrack ([&] (MBoxTrack& t)
                                    {
                                        auto grid = t.getBankSlots();
                                        grid[(size_t) row].first = *position;
                                        return t.setBankSlots (std::move (grid), MBoxApplication::numBanks, MBoxBank::numSlots);
                                    },
                                    "Bankslot switch moved to " + UIHelpers::positionText (*position));
    }

    juce::Result addRow() override
    {
        auto* track = physicalTrack();

        if (track == nullptr)
            return juce::Result::fail ("Select a Physical track first");

        const auto position = Bar { m_session.getCursor().bar, 1, 1, 0 };

        if (rowForPosition (position) >= 0)
            return juce::Result::fail ("There is already a switch at bar " + juce::String (position.bar) + ": move the edit cursor first");

        BankSlotRef ref;

        for (const auto& [bar, existing] : track->getBankSlots())
            if (! (position < bar))
                ref = existing;

        const auto result = m_session.editTrack ([&] (MBoxTrack& t)
                                                 {
                                                     auto grid = t.getBankSlots();
                                                     grid.emplace_back (position, ref);
                                                     return t.setBankSlots (std::move (grid), MBoxApplication::numBanks, MBoxBank::numSlots);
                                                 },
                                                 "Bankslot switch added at bar " + juce::String (position.bar));
        selectRow (rowForPosition (position));
        return result;
    }

    juce::Result deleteRow (int row) override
    {
        return m_session.editTrack ([&] (MBoxTrack& t)
                                    {
                                        auto grid = t.getBankSlots();
                                        grid.erase (grid.begin() + row);
                                        return t.setBankSlots (std::move (grid), MBoxApplication::numBanks, MBoxBank::numSlots);
                                    },
                                    "Bankslot switch deleted");
    }
};

//==============================================================================
ListEditorPanel::ListEditorPanel (Session& session)
    : ui::Panel ("Editor"), m_session (session)
{
    addAndMakeVisible (m_tabs);
    addChildComponent (m_lockedNote);
    m_lockedNote.setJustificationType (juce::Justification::centred);

    m_pages.push_back (std::make_unique<SongPage> (session));
    m_pages.push_back (std::make_unique<TrackPage> (session));
    m_pages.push_back (std::make_unique<TempoTable> (session));
    m_pages.push_back (std::make_unique<SignatureTable> (session));
    m_pages.push_back (std::make_unique<BankslotTable> (session));

    for (auto& page : m_pages)
        addChildComponent (*page);

    m_tabs.onChange = [this] (int index)
    {
        for (size_t i = 0; i < m_pages.size(); ++i)
            m_pages[i]->setVisible ((int) i == index);
    };

    m_tabs.setSelectedIndex (2, juce::dontSendNotification);
    m_pages[2]->setVisible (true);

    m_session.addListener (this);
    sessionChanged();
}

ListEditorPanel::~ListEditorPanel()
{
    m_session.removeListener (this);
}

void ListEditorPanel::showTab (Session::ListTab tab)
{
    m_tabs.setSelectedIndex ((int) tab);
}

void ListEditorPanel::resized()
{
    auto area = getContentBounds();
    m_tabs.setBounds (area.removeFromTop (32));
    area.removeFromTop (8);

    if (m_lockedNote.isVisible())
        m_lockedNote.setBounds (area.removeFromTop (20));

    area.removeFromTop (6);

    for (auto& page : m_pages)
        page->setBounds (area);
}

void ListEditorPanel::sessionChanged()
{
    const auto locked = m_session.isEditLocked();

    if (locked != m_lockedNote.isVisible())
    {
        m_lockedNote.setVisible (locked);
        resized();
    }

    dynamic_cast<SongPage&> (*m_pages[0]).refresh();
    dynamic_cast<TrackPage&> (*m_pages[1]).refresh();

    for (size_t i = 2; i < m_pages.size(); ++i)
        dynamic_cast<EventTable&> (*m_pages[i]).refresh();
}

void ListEditorPanel::showInList (Session::ListTab tab, const Bar& position)
{
    showTab (tab);

    if ((int) tab >= 2)
    {
        auto& table = dynamic_cast<EventTable&> (*m_pages[(size_t) tab]);
        table.refresh();
        table.selectRow (table.rowForPosition (position));
    }
}
