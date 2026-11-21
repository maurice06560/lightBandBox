#include "ListEditorPanel.h"
#include "TempoMap.h"

namespace
{
    /** "Field name" + control rows used by the Song and Track pages. */
    struct FormRow
    {
        std::unique_ptr<ui::Label> caption;
        juce::Component* control;
        int height;
    };

    /** Lays out the rows whose control is visible, from the top; returns the height used. */
    int layoutForm (std::vector<FormRow>& rows, juce::Rectangle<int> area, int captionWidth)
    {
        const auto top = area.getY();

        for (auto& row : rows)
        {
            if (row.control != nullptr && ! row.control->isVisible())
                continue;

            auto line = area.removeFromTop (row.height);
            area.removeFromTop (8);

            if (row.caption != nullptr)
                row.caption->setBounds (line.removeFromLeft (captionWidth));

            if (row.control != nullptr)
                row.control->setBounds (line);
        }

        return area.getY() - top;
    }

    /** Shows or hides a row (its caption too). */
    void showRow (std::vector<FormRow>& rows, juce::Component& control, bool shouldShow)
    {
        for (auto& row : rows)
        {
            if (row.control != &control)
                continue;

            control.setVisible (shouldShow);

            if (row.caption != nullptr)
                row.caption->setVisible (shouldShow);
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

    /** Parses a position typed by the user and checks it against the song's signatures. */
    juce::Result parseSongPosition (const MBoxSong& song, const juce::String& text, Bar& result)
    {
        const auto position = UIHelpers::parsePosition (text);

        if (! position.has_value())
            return juce::Result::fail ("Invalid position \"" + text + "\" (e.g. 5 or 5 2 1 0)");

        if (! song.getTimeline().isValidPosition (*position))
            return juce::Result::fail ("Position " + UIHelpers::positionText (*position) + " doesn't exist in this song's signatures");

        result = *position;
        return juce::Result::ok();
    }
}

//==============================================================================
int ListEditorPanel::idForOutputs (Outputs outputs)
{
    const auto [first, second] = outputs;

    if (first < 0)
        return noOutputsId;

    return second < 0 ? 2000 + first + 1 : 1000 + (first + 1) * 100 + second + 1;
}

ListEditorPanel::Outputs ListEditorPanel::outputsForId (int id)
{
    if (id >= 2000)
        return { id - 2000 - 1, -1 };

    if (id >= 1000)
        return { (id - 1000) / 100 - 1, (id - 1000) % 100 - 1 };

    return noOutputs;
}

juce::String ListEditorPanel::outputsText (Outputs outputs)
{
    const auto [first, second] = outputs;

    if (first < 0)
        return "N/A";

    return second < 0 ? juce::String (first + 1) + " mono" : juce::String (first + 1) + "/" + juce::String (second + 1);
}

juce::PopupMenu ListEditorPanel::outputsMenu (const std::vector<std::string>& outputNames, Outputs current, const juce::String& noneText)
{
    juce::PopupMenu menu;
    const auto count = juce::jmax (2, (int) outputNames.size());
    auto name = [&] (int channel) { return channel < (int) outputNames.size() ? juce::String (outputNames[(size_t) channel]) : juce::String(); };
    const auto currentId = idForOutputs (current);
    auto found = false;

    auto add = [&] (int id, const juce::String& text)
    {
        found = found || id == currentId;
        menu.addItem (id, text, true, id == currentId);
    };

    add (noOutputsId, noneText);
    menu.addSectionHeader ("Stereo");

    for (int left = 0; left + 1 < count; left += 2)
        add (idForOutputs ({ left, left + 1 }), juce::String (left + 1) + "/" + juce::String (left + 2) + "   "
                                                    + name (left) + (name (left).isNotEmpty() ? " + " : "") + name (left + 1));

    menu.addSectionHeader ("Mono");

    for (int channel = 0; channel < count; ++channel)
        add (idForOutputs ({ channel, -1 }), juce::String (channel + 1) + "   " + name (channel));

    // Outputs the current device doesn't have (e.g. saved with another device) stay listed
    if (! found)
        menu.addItem (currentId, outputsText (current), true, true);

    return menu;
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

        makeEditable (m_leftLocator, [this] (const juce::String& text) { setLocator (text, true); });
        makeEditable (m_rightLocator, [this] (const juce::String& text) { setLocator (text, false); });

        makeEditable (m_totalBars, [this] (const juce::String& text)
        {
            const auto bars = text.trim().getIntValue();

            if (bars < 1)
            {
                m_session.setStatus ("A song has at least one bar", true);
                refresh();
                return;
            }

            m_session.editSong ([bars] (MBoxSong& s)
                                {
                                    s.setTotalBar (bars);
                                    return s.getTotalBar() == bars ? juce::Result::ok()
                                                                   : juce::Result::fail ("The song can't end before its right locator: move it first");
                                },
                                "The song has " + juce::String (bars) + " bars");
        });

        m_metronome.addItem ("Off", offId);
        m_metronome.addItem ("Count-in only", countInId);
        m_metronome.addItem ("On", onId);
        m_metronome.setTooltip ("Count-in only: the " + juce::String (countInBeats) + "-beat count-in plays, then the click stays silent");
        m_metronome.onChange = [this]
        {
            const auto id = m_metronome.getSelectedId();
            m_session.setMetronomeState (id == offId ? MetronomeState::Off : id == countInId ? MetronomeState::FourCountdownOnly : MetronomeState::On);
        };

        m_volume.setRange (-60.0, maxVolumeDb, 0.5);
        m_volume.setTextValueSuffix (" dB");
        m_volume.setTooltip ("The click's level in the mix shown in the mixer, for every song");
        m_volume.onValueChange = [this]
        {
            m_session.setMetronomeVolume (m_volume.getValue());
            m_volumeValue.setText (UIHelpers::volumeText (m_volume.getValue()), juce::dontSendNotification);
        };
        m_volumeRow.addAndMakeVisible (m_volume);
        m_volumeRow.addAndMakeVisible (m_volumeValue);

        m_rows.push_back ({ caption ("Name"), &m_name, 30 });
        m_rows.push_back ({ caption ("Left locator"), &m_leftLocator, 30 });
        m_rows.push_back ({ caption ("Right locator"), &m_rightLocator, 30 });
        m_rows.push_back ({ caption ("Bars"), &m_totalBars, 30 });
        m_rows.push_back ({ caption ("Length"), &m_length, 30 });
        m_rows.push_back ({ nullptr, &m_metronomeHeading, 30 });
        m_rows.push_back ({ caption ("Click"), &m_metronome, 34 });
        m_rows.push_back ({ caption ("Level"), &m_volumeRow, 30 });

        for (auto& row : m_rows)
        {
            if (row.caption != nullptr)
                addAndMakeVisible (*row.caption);

            addAndMakeVisible (row.control);
        }

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
        m_leftLocator.setText (UIHelpers::positionText (song->getLeftLocator()), juce::dontSendNotification);
        m_rightLocator.setText (UIHelpers::positionText (song->getRightLocator()), juce::dontSendNotification);
        m_totalBars.setText (juce::String (song->getTotalBar()), juce::dontSendNotification);

        // The metronome stays editable while playing or paused; the song's structure doesn't
        const auto unlocked = ! m_session.isEditLocked();

        for (auto* label : { &m_name, &m_leftLocator, &m_rightLocator, &m_totalBars })
            label->setEnabled (unlocked);

        // What plays: from the left locator to the right one
        const TempoMap tempoMap (*song);
        m_length.setText (UIHelpers::timeText (tempoMap.toSeconds (song->getRightLocator()) - tempoMap.toSeconds (song->getLeftLocator())),
                          juce::dontSendNotification);

        const auto state = song->getMetronomeState();
        m_metronome.setSelectedId (state == MetronomeState::Off ? offId : state == MetronomeState::FourCountdownOnly ? countInId : onId,
                                   juce::dontSendNotification);

        const auto& bus = m_session.getSelectedMixBus();

        if (! m_volume.isMouseButtonDown())
            m_volume.setValue (bus.getMetronomeVolume(), juce::dontSendNotification);

        m_volumeValue.setText (UIHelpers::volumeText (bus.getMetronomeVolume()) + " in " + bus.getName(), juce::dontSendNotification);
    }

    void resized() override
    {
        auto area = getLocalBounds();
        m_placeholder.setBounds (area.withHeight (60));

        layoutForm (m_rows, area, 100);

        auto volume = m_volumeRow.getLocalBounds();
        m_volumeValue.setBounds (volume.removeFromRight (volume.getWidth() * 2 / 5));
        m_volume.setBounds (volume.reduced (0, 4));
    }

private:
    void setLocator (const juce::String& text, bool left)
    {
        auto* song = m_session.getSelectedSong();
        Bar position;

        if (song == nullptr)
            return;

        if (const auto result = parseSongPosition (*song, text, position); result.failed())
        {
            m_session.report (result);
            refresh();
            return;
        }

        m_session.editSong ([position, left] (MBoxSong& s) { return left ? s.setLeftLocator (position) : s.setRightLocator (position); },
                            (left ? "Left locator at " : "Right locator at ") + UIHelpers::positionText (position));
    }

    Session& m_session;
    std::vector<FormRow> m_rows;

    static constexpr int offId = 1, countInId = 2, onId = 3;

    ui::Label m_name { {}, ui::Label::Style::Value };
    ui::Label m_leftLocator { {}, ui::Label::Style::Value };
    ui::Label m_rightLocator { {}, ui::Label::Style::Value };
    ui::Label m_totalBars { {}, ui::Label::Style::Value };
    ui::Label m_length { {}, ui::Label::Style::Body };
    ui::Label m_metronomeHeading { "Metronome", ui::Label::Style::Heading };
    juce::ComboBox m_metronome;
    juce::Component m_volumeRow;
    ui::Fader m_volume { ui::Fader::Orientation::Horizontal };
    ui::Label m_volumeValue { {}, ui::Label::Style::Caption };
    ui::Label m_placeholder { "No song selected", ui::Label::Style::Caption };
};

//==============================================================================
/** The selected track: name, type, input and DI output (physical), WAV (audio), and its volume and
    mute / solo in the selected mix bus.
*/
class TrackPage final : public juce::Component
{
public:
    explicit TrackPage (Session& session) : m_session (session)
    {
        m_input.onChange = [this]
        {
            const auto channel = m_input.getSelectedId() - 1;
            editPhysical ([channel] (MBoxPhysicalTrack& t) { t.setPhysicalInput (channel); }, "Input " + juce::String (channel + 1));
        };

        m_diOutput.setTooltip ("Sends this track's own sound (after its bankslot effects) straight to an output, before any mix");
        m_diOutput.onChange = [this]
        {
            const auto id = m_diOutput.getSelectedId();

            if (id == 0)
                return;

            const auto output = id == noneId ? -1 : id - 2;

            // Deferred: the edit refreshes this page, which refills the box
            juce::MessageManager::callAsync ([safe = juce::Component::SafePointer<TrackPage> (this), output]
            {
                if (safe != nullptr)
                    safe->editPhysical ([output] (MBoxPhysicalTrack& t) { t.setDIOutput (output); },
                                        output < 0 ? juce::String ("DI output off") : "DI output " + juce::String (output + 1));
            });
        };

        makeEditable (m_start, [this] (const juce::String& text)
        {
            auto* song = m_session.getSelectedSong();
            Bar position;

            if (song == nullptr)
                return;

            if (const auto result = parseSongPosition (*song, text, position); result.failed())
            {
                m_session.report (result);
                refresh();
                return;
            }

            m_session.moveWav (m_session.getSelectedTrackId(), position);
        });

        makeEditable (m_name, [this] (const juce::String& text)
        {
            const auto name = text.trim();
            m_session.editTrack ([name] (MBoxTrack& t)
                                 {
                                     if (name.isEmpty())
                                         return juce::Result::fail ("A track needs a name");

                                     t.setName (name);
                                     return juce::Result::ok();
                                 },
                                 "Track renamed to " + name);
        });

        makeEditable (m_volume, [this] (const juce::String& text)
        {
            const auto silent = text.containsIgnoreCase ("inf");
            const auto value = silent ? minVolumeDb : (double) text.upToFirstOccurrenceOf ("dB", false, true).getFloatValue();
            m_session.setTrackVolume (m_session.getSelectedTrackId(), value);
        });

        m_choose.onClick = [this] { chooseWav(); };
        m_clear.onClick = [this]
        {
            m_session.editTrack ([] (MBoxTrack& t)
                                 {
                                     auto* audio = dynamic_cast<MBoxAudioTrack*> (&t);

                                     if (audio == nullptr)
                                         return juce::Result::fail ("Not a WAV track");

                                     audio->setFilename ({}, 0);
                                     return juce::Result::ok();
                                 },
                                 "WAV removed from the track (the file stays in the song's media folder)");
        };

        // Live, like volume: never greyed by playback
        m_mute.onClick = [this] { m_session.setTrackMute (m_session.getSelectedTrackId(), m_mute.getToggleState()); };
        m_solo.onClick = [this] { m_session.setTrackSolo (m_session.getSelectedTrackId(), m_solo.getToggleState()); };
        m_mute.setTooltip ("Mute this track");
        m_solo.setTooltip ("Solo: only soloed tracks play");
        m_muteSolo.addAndMakeVisible (m_mute);
        m_muteSolo.addAndMakeVisible (m_solo);

        m_rows.push_back ({ nullptr, &m_title, 30 });
        m_rows.push_back ({ caption ("Name"), &m_name, 30 });
        m_rows.push_back ({ caption ("Type"), &m_type, 30 });
        m_rows.push_back ({ caption ("Input"), &m_input, 28 });
        m_rows.push_back ({ caption ("DI output"), &m_diOutput, 28 });
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

        auto* audio = dynamic_cast<MBoxAudioTrack*> (track);
        auto* physical = dynamic_cast<MBoxPhysicalTrack*> (track);
        const auto hasFile = audio != nullptr && audio->getFilename() != juce::File();

        m_title.setText ("Track " + juce::String (track->getId()), juce::dontSendNotification);
        m_name.setText (track->getName(), juce::dontSendNotification);
        m_type.setText (UIHelpers::trackTypeName (track->getType()), juce::dontSendNotification);

        // The mix changes live; the rest of the track waits for playback to stop
        const auto unlocked = ! m_session.isEditLocked();
        m_name.setEnabled (unlocked);

        const auto device = m_session.getCore().getCurrentDevice();

        // Input: the device's input channels (or 1..8 when no device is open)
        m_input.clear (juce::dontSendNotification);

        for (int channel = 0; channel < juce::jmax (8, device.m_numInputs); ++channel)
            m_input.addItem (juce::String (channel + 1) + "   " + (channel < device.m_numInputs ? juce::String (device.m_inputNames[(size_t) channel])
                                                                                               : "Input " + juce::String (channel + 1)),
                             channel + 1);

        m_input.setSelectedId (physical != nullptr ? physical->getPhysicalInput() + 1 : 0, juce::dontSendNotification);
        m_input.setEnabled (physical != nullptr && unlocked);

        // DI output: none, or one of the device's outputs
        m_diOutput.clear (juce::dontSendNotification);
        m_diOutput.addItem ("None", noneId);

        for (int channel = 0; channel < juce::jmax (2, device.m_numOutputs); ++channel)
            m_diOutput.addItem (juce::String (channel + 1) + "   " + (channel < device.m_numOutputs ? juce::String (device.m_outputNames[(size_t) channel])
                                                                                                  : "Output " + juce::String (channel + 1)),
                                channel + 2);

        const auto di = physical != nullptr ? physical->getDIOutput() : -1;
        m_diOutput.setSelectedId (di < 0 ? noneId : di + 2, juce::dontSendNotification);
        m_diOutput.setEnabled (physical != nullptr && unlocked);

        m_file.setText (hasFile ? audio->getFilename().getFileName() : juce::String (audio != nullptr ? "None" : "-"), juce::dontSendNotification);
        m_start.setText (hasFile ? UIHelpers::positionText (audio->getAudioStart()) : juce::String ("-"), juce::dontSendNotification);
        m_choose.setEnabled (audio != nullptr && unlocked);
        m_clear.setEnabled (hasFile && unlocked);
        m_start.setEnabled (hasFile && unlocked);
        m_file.setEnabled (audio != nullptr);

        // Volume and mute / solo belong to the bus shown in the mixer
        const auto* trackMix = m_session.getTrackMix (track->getId());
        m_mix.setText (m_session.getSelectedMixBus().getName(), juce::dontSendNotification);
        m_volume.setText (trackMix != nullptr ? UIHelpers::volumeText (trackMix->m_volume) : juce::String ("-"), juce::dontSendNotification);
        m_mute.setToggleState (trackMix != nullptr && trackMix->m_mute, juce::dontSendNotification);
        m_solo.setToggleState (trackMix != nullptr && trackMix->m_solo, juce::dontSendNotification);

        // Only the rows of this kind of track
        for (auto* control : std::initializer_list<juce::Component*> { &m_input, &m_diOutput })
            showRow (m_rows, *control, physical != nullptr);

        for (auto* control : std::initializer_list<juce::Component*> { &m_file, &m_wavButtons, &m_start })
            showRow (m_rows, *control, audio != nullptr);

        resized();

        m_hint.setText (audio != nullptr    ? "The WAV starts at its position and plays to its end; the right locator follows it if it ends later."
                        : physical != nullptr ? "Plays the input live through the bankslots set in the Bankslots list."
                                              : "Plays cue clicks at the positions added in the arrangement.",
                        juce::dontSendNotification);
    }

    void resized() override
    {
        auto area = getLocalBounds();
        m_placeholder.setBounds (area.withHeight (60));
        area.removeFromTop (layoutForm (m_rows, area, 90));

        auto muteSolo = m_muteSolo.getLocalBounds();
        m_mute.setBounds (muteSolo.removeFromLeft (muteSolo.getWidth() / 2).reduced (2, 0));
        m_solo.setBounds (muteSolo.reduced (2, 0));
        m_hint.setBounds (area.removeFromTop (44));

        auto buttons = m_wavButtons.getLocalBounds().withTrimmedLeft (90);
        m_choose.setBounds (buttons.removeFromLeft (buttons.getWidth() / 2).reduced (2, 0));
        m_clear.setBounds (buttons.reduced (2, 0));
    }

private:
    static constexpr int noneId = 1;

    void editPhysical (const std::function<void (MBoxPhysicalTrack&)>& edit, const juce::String& message)
    {
        m_session.editTrack ([&edit] (MBoxTrack& t)
                             {
                                 auto* physical = dynamic_cast<MBoxPhysicalTrack*> (&t);

                                 if (physical == nullptr)
                                     return juce::Result::fail ("Not a physical track");

                                 edit (*physical);
                                 return juce::Result::ok();
                             },
                             message);
    }

    void chooseWav()
    {
        auto* audio = dynamic_cast<MBoxAudioTrack*> (m_session.getSelectedTrack());

        if (audio == nullptr || ! m_session.canEdit())
            return;

        const auto trackId = audio->getId();
        const auto start = audio->getFilename() != juce::File() ? audio->getAudioStart() : m_session.getCursor();

        m_chooser = std::make_unique<juce::FileChooser> ("Choose a WAV (PCM 16-bit, 44.1 kHz, stereo)",
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
    ui::Label m_type { {}, ui::Label::Style::Body };
    juce::ComboBox m_input;
    juce::ComboBox m_diOutput;
    ui::Label m_mix { {}, ui::Label::Style::Body };
    ui::Label m_file { {}, ui::Label::Style::Body };
    juce::Component m_wavButtons;
    ui::Button m_choose { "Choose..." };
    ui::Button m_clear { "Clear" };
    ui::Label m_start { {}, ui::Label::Style::Value };
    ui::Label m_volume { {}, ui::Label::Style::Value };
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

        Bar position;

        if (auto r = parseSongPosition (*song, text, position); r.failed())
            return r;

        return m_session.editSong ([&] (MBoxSong& s)
                                   {
                                       s.removeTempo (bar);

                                       if (auto r = s.addTempo ({ position, bpm }); r.failed())
                                       {
                                           s.addTempo ({ bar, bpm });
                                           return r;
                                       }

                                       return juce::Result::ok();
                                   },
                                   "Tempo moved to " + UIHelpers::positionText (position));
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
            if (grid[i].first.m_bar == position.m_bar)
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
            case 1:  return juce::String (bar.m_bar);
            case 2:  return UIHelpers::signatureText (signature);
            default: return juce::String (signature.m_numerator) + " per bar";
        }
    }

    bool isCellEditable (int row, int columnId) override { return columnId == 2 || (columnId == 1 && row > 0); }

    juce::Result setCellText (int row, int columnId, const juce::String& text) override
    {
        const auto [bar, signature] = m_session.getSelectedSong()->getSignatureGrid()[(size_t) row];

        if (columnId == 2)
        {
            const auto parsed = UIHelpers::parseSignature (text);

            if (! parsed.has_value())
                return juce::Result::fail ("Invalid signature \"" + text + "\": numerator 1-32, denominator 2, 4, 8 or 16");

            return m_session.editSong ([&] (MBoxSong& s) { return s.addSignature ({ bar, *parsed }); },
                                       UIHelpers::signatureText (*parsed) + " from bar " + juce::String (bar.m_bar));
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
        const auto position = Bar { m_session.getCursor().m_bar, 1, 1, 0 };

        if (song == nullptr)
            return juce::Result::fail ("No song selected");

        if (rowForPosition (position) >= 0)
            return juce::Result::fail ("Bar " + juce::String (position.m_bar) + " already has a signature: move the edit cursor first");

        const auto signature = song->getTimeline().getSignatureAtBar (position.m_bar);
        const auto result = m_session.editSong ([&] (MBoxSong& s) { return s.addSignature ({ position, signature }); },
                                                "Signature added at bar " + juce::String (position.m_bar) + ": double-click it to change it");
        selectRow (rowForPosition (position));
        return result;
    }

    juce::Result deleteRow (int row) override
    {
        const auto bar = m_session.getSelectedSong()->getSignatureGrid()[(size_t) row].first;
        return m_session.editSong ([&] (MBoxSong& s) { return s.removeSignature (bar); }, "Signature at bar " + juce::String (bar.m_bar) + " deleted");
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
        return track != nullptr ? (int) track->getBankSlotChanges().size() : 0;
    }

    int rowForPosition (const Bar& position) override
    {
        auto* track = physicalTrack();

        if (track == nullptr)
            return -1;

        const auto& grid = track->getBankSlotChanges();

        for (size_t i = 0; i < grid.size(); ++i)
            if (grid[i].first == position)
                return (int) i;

        return -1;
    }

protected:
    MBoxPhysicalTrack* physicalTrack()
    {
        return dynamic_cast<MBoxPhysicalTrack*> (m_session.getSelectedTrack());
    }

    juce::String getCaption() override
    {
        auto* track = m_session.getSelectedTrack();

        if (track == nullptr)
            return "No track selected";

        return track->getName() + (physicalTrack() != nullptr ? " bankslots" : " is not a physical track");
    }

    juce::String getHint() override
    {
        return physicalTrack() != nullptr ? "From each position the track's input plays through that bankslot's effects, until the next switch."
                                          : "Bankslots apply to Physical tracks: select one in the arrangement.";
    }

    bool canAdd() override { return physicalTrack() != nullptr; }

    bool hasCustomCell (int columnId) override { return columnId == 2; }

    static int idFor (MBoxSlotRef ref)        { return (ref.m_bank - 1) * numberOfSlotsPerBanks + ref.m_slot; }
    static MBoxSlotRef refFor (int id)        { return { (id - 1) / numberOfSlotsPerBanks + 1, (id - 1) % numberOfSlotsPerBanks + 1 }; }

    juce::Component* createCustomCell (int row, int, juce::Component* existing) override
    {
        auto* box = dynamic_cast<juce::ComboBox*> (existing);

        if (box == nullptr)
        {
            delete existing;
            box = new juce::ComboBox();
        }

        box->clear (juce::dontSendNotification);
        const auto banks = m_session.getBanks();

        for (int b = 1; b <= numberOfBanks; ++b)
        {
            box->addSectionHeading (banks[(size_t) b - 1].m_name);

            for (int slot = 1; slot <= numberOfSlotsPerBanks; ++slot)
                box->addItem (UIHelpers::bankSlotName (banks, { b, slot }), idFor ({ b, slot }));
        }

        const auto ref = physicalTrack()->getBankSlotChanges()[(size_t) row].second;
        box->setSelectedId (idFor (ref), juce::dontSendNotification);
        box->setEnabled (! m_session.isEditLocked());
        box->setColour (juce::ComboBox::backgroundColourId, UIHelpers::bankColour (ref.m_bank).withAlpha (0.35f).darker (0.4f));
        box->onChange = [this, row, box]
        {
            // As with text cells: apply after the callback, since the edit rebuilds this combo box
            juce::MessageManager::callAsync ([table = juce::Component::SafePointer<BankslotTable> (this), row, id = box->getSelectedId()]
            {
                if (table != nullptr && id > 0)
                    table->setBankslot (row, refFor (id));
            });
        };

        return box;
    }

    /** Applies an edit to the selected physical track. */
    juce::Result editPhysical (const std::function<juce::Result (MBoxPhysicalTrack&)>& edit, const juce::String& message)
    {
        return m_session.editTrack ([&edit] (MBoxTrack& t)
                                    {
                                        auto* physical = dynamic_cast<MBoxPhysicalTrack*> (&t);
                                        return physical != nullptr ? edit (*physical) : juce::Result::fail ("Not a physical track");
                                    },
                                    message);
    }

    void setBankslot (int row, MBoxSlotRef newRef)
    {
        auto* track = physicalTrack();

        if (track == nullptr || row >= (int) track->getBankSlotChanges().size())
            return;

        const auto position = track->getBankSlotChanges()[(size_t) row].first;

        editPhysical ([&] (MBoxPhysicalTrack& t) { t.setBankSlotChange (position, newRef); return juce::Result::ok(); },
                      UIHelpers::bankSlotName (m_session.getBanks(), newRef) + " from " + UIHelpers::positionText (position));
    }

    juce::String getCellText (int row, int columnId) override
    {
        const auto& [bar, ref] = physicalTrack()->getBankSlotChanges()[(size_t) row];

        if (columnId == 1)
            return UIHelpers::positionText (bar);

        return UIHelpers::bankSlotEffects (m_session.getBanks(), ref);
    }

    bool isCellEditable (int, int columnId) override { return columnId == 1; }

    juce::Result setCellText (int row, int, const juce::String& text) override
    {
        auto* song = m_session.getSelectedSong();
        Bar position;

        if (auto r = parseSongPosition (*song, text, position); r.failed())
            return r;

        const auto [bar, ref] = physicalTrack()->getBankSlotChanges()[(size_t) row];

        if (rowForPosition (position) >= 0 && position != bar)
            return juce::Result::fail ("There is already a switch at " + UIHelpers::positionText (position));

        return editPhysical ([bar, ref, position] (MBoxPhysicalTrack& t)
                             {
                                 t.removeBankSlotChange (bar);
                                 t.setBankSlotChange (position, ref);
                                 return juce::Result::ok();
                             },
                             "Bankslot switch moved to " + UIHelpers::positionText (position));
    }

    juce::Result addRow() override
    {
        auto* track = physicalTrack();

        if (track == nullptr)
            return juce::Result::fail ("Select a Physical track first");

        const auto position = Bar { m_session.getCursor().m_bar, 1, 1, 0 };

        if (rowForPosition (position) >= 0)
            return juce::Result::fail ("There is already a switch at bar " + juce::String (position.m_bar) + ": move the edit cursor first");

        // Continue with the bankslot already active there, or Bank 1 / Slot 1
        const auto ref = track->getSlotAt (position).value_or (MBoxSlotRef {});
        const auto result = editPhysical ([&] (MBoxPhysicalTrack& t) { t.setBankSlotChange (position, ref); return juce::Result::ok(); },
                                          "Bankslot switch added at bar " + juce::String (position.m_bar));
        selectRow (rowForPosition (position));
        return result;
    }

    juce::Result deleteRow (int row) override
    {
        const auto position = physicalTrack()->getBankSlotChanges()[(size_t) row].first;
        return editPhysical ([position] (MBoxPhysicalTrack& t)
                             {
                                 return t.removeBankSlotChange (position) ? juce::Result::ok() : juce::Result::fail ("No switch there");
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
