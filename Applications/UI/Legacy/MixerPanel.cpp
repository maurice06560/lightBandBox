#include "MixerPanel.h"
#include "ListEditorPanel.h"
#include "PluginMenu.h"
#include "../Audio/MBoxAudioEngine.h"

/** A strip's insert plugins as a column of pills, in processing order, then an "Add" pill while
    there is room. Clicking a plugin opens its window (allowed during playback, its changes are
    heard live); its × (shown on hover) or its right-click menu removes it. Adding and removing are
    refused while a song plays or is paused.
*/
class MixerPanel::InsertSlots final : public juce::Component,
                                      public juce::TooltipClient
{
public:
    static constexpr int rowHeight = 18;
    static constexpr int rowGap = 2;

    InsertSlots (Session& session, int stripId) : m_session (session), m_stripId (stripId) {}

    std::function<juce::String()> stripName;

    void paint (juce::Graphics& g) override
    {
        const auto* inserts = m_session.getInserts (m_stripId);

        if (inserts == nullptr)
            return;

        const auto& palette = ui::getTheme (*this).getPalette();
        const auto locked = m_session.isEditLocked();
        g.setFont (ui::LookAndFeel::makeFont (10.0f, true));

        for (int row = 0; row <= juce::jmin (inserts->size(), MBoxInserts::maxInserts - 1); ++row)
        {
            const auto area = rowBounds (row).toFloat();

            if (row == inserts->size())
            {
                // The Add pill: an outline while editing is possible.
                const auto colour = locked ? palette.textDim.withAlpha (0.4f) : palette.textDim;
                g.setColour (colour.withAlpha (row == m_hoverRow && ! locked ? 0.35f : 0.15f));
                g.fillRoundedRectangle (area, area.getHeight() / 2.0f);
                g.setColour (colour);
                g.drawRoundedRectangle (area.reduced (0.5f), area.getHeight() / 2.0f, 1.0f);
                g.setColour (locked ? palette.textDim.withAlpha (0.5f) : palette.text);
                g.drawText ("Add", area, juce::Justification::centred, false);
                continue;
            }

            const auto* plugin = dynamic_cast<const MAudioEffect*> (&(*inserts)[row]);
            const auto missing = plugin != nullptr && plugin->isMissing();
            const auto base = missing ? palette.accentWarm : palette.accent;
            const auto hovered = row == m_hoverRow;

            g.setGradientFill (juce::ColourGradient (base.withAlpha (hovered ? 0.65f : 0.45f), area.getTopLeft(),
                                                     base.darker (0.6f).withAlpha (0.55f), area.getBottomLeft(), false));
            g.fillRoundedRectangle (area, area.getHeight() / 2.0f);
            g.setColour (base.withAlpha (0.8f));
            g.drawRoundedRectangle (area.reduced (0.5f), area.getHeight() / 2.0f, 1.0f);

            auto text = area.reduced (6.0f, 0.0f);

            if (hovered && ! locked)
            {
                g.setColour (juce::Colours::white.withAlpha (m_hoverRemove ? 1.0f : 0.6f));
                g.drawText (juce::String (juce::CharPointer_UTF8 ("\xc3\x97")), removeBounds (row).toFloat(), juce::Justification::centred, false);
                text.removeFromRight (8.0f);
            }

            g.setColour (juce::Colours::white.withAlpha (missing ? 0.7f : 0.95f));
            g.drawText ((*inserts)[row].getName(), text, juce::Justification::centred, true);
        }
    }

    void mouseMove (const juce::MouseEvent& e) override  { updateHover (e.getPosition()); }
    void mouseEnter (const juce::MouseEvent& e) override { updateHover (e.getPosition()); }
    void mouseExit (const juce::MouseEvent&) override    { updateHover ({ -1, -1 }); }

    void mouseUp (const juce::MouseEvent& e) override
    {
        auto* inserts = m_session.getInserts (m_stripId);
        const auto row = rowAt (e.getPosition());

        if (inserts == nullptr || row < 0 || e.mouseWasDraggedSinceMouseDown())
            return;

        if (row == inserts->size())
        {
            if (! e.mods.isPopupMenu())
                showAddMenu();

            return;
        }

        if (e.mods.isPopupMenu())
            showInsertMenu (row);
        else if (removeBounds (row).contains (e.getPosition()) && ! m_session.isEditLocked())
            remove (row);
        else
            openWindow (row);
    }

    juce::String getTooltip() override
    {
        const auto* inserts = m_session.getInserts (m_stripId);

        if (inserts == nullptr || m_hoverRow < 0)
            return {};

        if (m_hoverRow == inserts->size())
            return "Add a plugin to this strip (up to " + juce::String (MBoxInserts::maxInserts) + ")";

        if (const auto* plugin = dynamic_cast<const MAudioEffect*> (&(*inserts)[m_hoverRow]))
        {
            const auto& description = plugin->getDescription();

            if (plugin->isMissing())
                return description.name + " can't be loaded: " + plugin->getLoadError() + ". It passes audio through; its settings are kept.";

            return description.name + " by " + description.manufacturerName + " (" + PluginMenu::formatLabel (description)
                 + "). Click to open; right-click or x to remove.";
        }

        return (*inserts)[m_hoverRow].getName();
    }

    /** How many rows a strip with this many inserts shows. */
    static int rowsFor (int numInserts) { return juce::jmin (numInserts + 1, MBoxInserts::maxInserts); }
    static int heightFor (int rows)     { return rows * (rowHeight + rowGap); }

private:
    /** The pills sit on the fader: the Add pill always directly above it, the plugins stacked above
        that in processing order (top first), so every strip's Add pill is on the same line.
    */
    juce::Rectangle<int> rowBounds (int row) const
    {
        const auto* inserts = m_session.getInserts (m_stripId);
        const auto shown = rowsFor (inserts != nullptr ? inserts->size() : 0);
        const auto top = getHeight() - heightFor (shown);
        return { 1, top + row * (rowHeight + rowGap), getWidth() - 2, rowHeight };
    }

    juce::Rectangle<int> removeBounds (int row) const
    {
        return rowBounds (row).removeFromRight (16);
    }

    int rowAt (juce::Point<int> position) const
    {
        const auto* inserts = m_session.getInserts (m_stripId);

        if (inserts == nullptr)
            return -1;

        for (int row = 0; row <= juce::jmin (inserts->size(), MBoxInserts::maxInserts - 1); ++row)
            if (rowBounds (row).contains (position))
                return row;

        return -1;
    }

    void updateHover (juce::Point<int> position)
    {
        const auto row = rowAt (position);
        const auto onRemove = row >= 0 && removeBounds (row).contains (position);

        if (row != m_hoverRow || onRemove != m_hoverRemove)
        {
            m_hoverRow = row;
            m_hoverRemove = onRemove;
            repaint();
        }
    }

    void showAddMenu()
    {
        if (! m_session.canEdit())
            return;

        PluginMenu::show (m_session, *this, [this] (const juce::PluginDescription& description)
                          {
                              // A new, independent instance: the same plugin on another strip keeps its own settings.
                              auto plugin = PluginMenu::create (m_session, description);

                              if (plugin == nullptr)
                                  return;

                              // The edit refreshes every panel, which may rebuild this component: don't use it afterwards.
                              auto& session = m_session;
                              auto* added = plugin.get();
                              const auto name = stripName != nullptr ? stripName() : juce::String();
                              const auto result = session.editInserts (m_stripId,
                                                                       [&] (MBoxInserts& inserts) { return inserts.add (std::move (plugin)); },
                                                                       description.name + " added to " + name);

                              if (result.wasOk())
                                  showEditor (session, *added, name);
                          });
    }

    void showInsertMenu (int row)
    {
        juce::PopupMenu menu;
        menu.addItem (1, "Open plugin window");
        menu.addSeparator();
        menu.addItem (2, "Remove", ! m_session.isEditLocked());

        menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this),
                            [safe = juce::Component::SafePointer<InsertSlots> (this), row] (int id)
                            {
                                if (safe == nullptr)
                                    return;

                                if (id == 1)
                                    safe->openWindow (row);
                                else if (id == 2)
                                    safe->remove (row);
                            });
    }

    void openWindow (int row)
    {
        auto* inserts = m_session.getInserts (m_stripId);

        if (inserts == nullptr || row >= inserts->size())
            return;

        if (auto* plugin = dynamic_cast<MAudioEffect*> (&(*inserts)[row]))
        {
            if (plugin->isMissing())
            {
                m_session.setStatus (plugin->getDescription().name + " can't be loaded: " + plugin->getLoadError(), true);
                return;
            }

            juce::MouseCursor::showWaitCursor();   // the first opening may load the plugin
            showEditor (m_session, *plugin, stripName != nullptr ? stripName() : juce::String());
            juce::MouseCursor::hideWaitCursor();
        }
    }

    void remove (int row)
    {
        auto* inserts = m_session.getInserts (m_stripId);

        if (inserts == nullptr || row >= inserts->size())
            return;

        const auto name = (*inserts)[row].getName();
        const auto strip = stripName != nullptr ? stripName() : juce::String();
        m_session.editInserts (m_stripId, [row] (MBoxInserts& i) { return i.remove (row); }, name + " removed from " + strip);
    }

    static void showEditor (Session& session, MAudioEffect& plugin, const juce::String& stripName)
    {
        // Settings changed in the window belong to the selected song. (The session outlives the songs' plugins.)
        plugin.onStateChanged = [&session] { session.markSongChanged(); };
        plugin.showEditor (plugin.getName() + " - " + stripName);
    }

    Session& m_session;
    const int m_stripId;
    int m_hoverRow = -1;
    bool m_hoverRemove = false;
};

/** A channel strip: insert plugins, fader with meter, and a coloured name plate. */
struct MixerPanel::Strip final : public juce::Component
{
    Strip (Session& session, int id, juce::Colour colour) : trackId (id), plateColour (colour)
    {
        addAndMakeVisible (fader);

        // Every strip but the Metronome's has inserts.
        if (trackId != clickId)
        {
            inserts = std::make_unique<InsertSlots> (session, trackId == masterId ? Session::masterStrip : trackId);
            inserts->stripName = [this] { return name; };
            addAndMakeVisible (*inserts);
        }

        // Track strips get Logic-style M / S buttons and balance knob. Master has output selector and balance. Metronome has click on/off.
        if (trackId == masterId)
        {
            output.setTooltip ("The outputs the master volume applies to");
            balance.setTooltip ("Balance: -64 (left) to +63 (right)");
            balance.setRange (-64.0, 63.0, 1.0);
            balance.setNumDecimalPlacesToDisplay (0);
            addAndMakeVisible (output);
            addAndMakeVisible (balance);
        }
        else if (trackId == clickId)
        {
            clickOn.setTooltip ("Whether the click plays in this mix (each mix has its own switch; the level is shared)");
            addAndMakeVisible (clickOn);
        }
        else
        {
            mute.setTooltip ("Mute");
            solo.setTooltip ("Solo: only soloed tracks play");
            balance.setTooltip ("Balance: -64 (left) to +63 (right)");
            balance.setRange (-64.0, 63.0, 1.0);
            balance.setNumDecimalPlacesToDisplay (0);
            addAndMakeVisible (mute);
            addAndMakeVisible (solo);
            addAndMakeVisible (balance);
        }
    }

    void resized() override
    {
        auto area = getLocalBounds();
        plate = area.removeFromBottom (22).reduced (1, 0);
        area.removeFromBottom (3);

        auto buttons = area.removeFromBottom (24);
        area.removeFromBottom (2);
        output.setBounds (buttons.reduced (1, 0));
        clickOn.setBounds (buttons.reduced (1, 0));
        mute.setBounds (buttons.removeFromLeft (buttons.getWidth() / 2).reduced (1, 0));
        solo.setBounds (buttons.reduced (1, 0));

        // Every strip reserves the same height so that the faders line up.
        auto insertArea = area.removeFromTop (InsertSlots::heightFor (insertRows));
        area.removeFromTop (2);

        if (inserts != nullptr)
            inserts->setBounds (insertArea);

        // Balance knob at the top of the fader area, between inserts and fader
        auto balanceArea = area.removeFromTop (35);
        area.removeFromTop (2);
        balance.setBounds (balanceArea.reduced (1, 0));

        fader.setBounds (area);
    }

    void paint (juce::Graphics& g) override
    {
        g.setColour (plateColour.withAlpha (selected ? 0.95f : 0.6f).darker (selected ? 0.0f : 0.3f));
        g.fillRoundedRectangle (plate.toFloat(), 4.0f);

        g.setColour (juce::Colours::white.withAlpha (selected ? 1.0f : 0.8f));
        g.setFont (ui::LookAndFeel::makeFont (11.0f, true));
        g.drawFittedText (name, plate.reduced (3, 0), juce::Justification::centred, 1, 0.85f);
    }

    /** Meter ballistics: instant attack, ~25 dB/s release; peak hold for 1.5 s. */
    void updateMeter (float peakGain)
    {
        const auto now = juce::Time::getMillisecondCounterHiRes();
        const auto db = juce::Decibels::gainToDecibels (peakGain, -96.0f);
        level = juce::jmax (db, level - 25.0f * (float) ((now - lastUpdate) / 1000.0));

        if (db >= peakHold || now - peakTime > 1500.0)
        {
            peakHold = juce::jmax (db, level);
            peakTime = now;
        }

        lastUpdate = now;
        fader.setMeterLevel (level, peakHold);
    }

    static constexpr int clickId = 0;
    static constexpr int masterId = -1;

    int trackId;
    int insertRows = 1;
    std::unique_ptr<InsertSlots> inserts;
    juce::Colour plateColour;
    bool selected = false;
    ui::ChannelFader fader;
    ui::Button mute { "M", ui::Button::Variant::Mute, true };
    ui::Button solo { "S", ui::Button::Variant::Solo, true };
    ui::Knob balance { "Balance", false };
    ui::Button output { "1/2" };
    ui::Button clickOn { "On", ui::Button::Variant::Standard, true };
    juce::String name;
    juce::Rectangle<int> plate;
    float level = -96.0f, peakHold = -96.0f;
    double peakTime = 0.0, lastUpdate = juce::Time::getMillisecondCounterHiRes();
};

//==============================================================================
MixerPanel::MixerPanel (Session& session)
    : ui::Panel ("Mixer"), m_session (session)
{
    m_viewport.setViewedComponent (&m_content, false);
    m_viewport.setScrollBarsShown (false, true);
    m_viewport.setScrollBarThickness (8);
    addAndMakeVisible (m_viewport);

    m_session.addListener (this);
    sessionChanged();
    startTimerHz (30);
}

MixerPanel::~MixerPanel()
{
    m_session.removeListener (this);
}

void MixerPanel::resized()
{
    m_viewport.setBounds (getContentBounds());
    layoutStrips();
}

void MixerPanel::layoutStrips()
{
    // Narrow strips, like Logic's mixer: the channel fader switches to its compact layout.
    constexpr int stripWidth = 60;
    constexpr int stripGap = 3;
    constexpr int clickGap = 12;   // sets the Metronome and Master strips apart, like Logic's output strips
    constexpr int balanceKnobHeight = 35;  // balance knob + gap
    constexpr int balanceKnobGap = 2;
    const auto height = m_viewport.getHeight() - (m_viewport.isHorizontalScrollBarShown() ? 10 : 0);
    // Add extra height for balance knob so faders don't get compressed
    const auto contentHeight = juce::jmax (0, height + balanceKnobHeight + balanceKnobGap);
    m_content.setSize (juce::jmax (m_viewport.getWidth(), (int) m_strips.size() * (stripWidth + stripGap) + 2 * clickGap), contentHeight);

    int x = 0;

    for (auto& strip : m_strips)
    {
        if (strip->trackId == Strip::clickId || strip->trackId == Strip::masterId)
            x += clickGap;

        strip->setBounds (x, 0, stripWidth, m_content.getHeight());
        x += stripWidth + stripGap;
    }
}

void MixerPanel::sessionChanged()
{
    auto* song = m_session.getSelectedSong();
    std::vector<int> ids;

    if (song != nullptr)
    {
        for (const auto& track : song->getTracks())
            ids.push_back (track.getId());

        // The metronome strip is there (in every mix) while the metronome is on.
        if (showsClick (*song))
            ids.push_back (Strip::clickId);

        ids.push_back (Strip::masterId);
    }

    if (ids != m_stripTrackIds)
        rebuildStrips();

    if (song == nullptr)
        return;

    // The insert area fits the busiest strip (one row more for its Add pill, up to the maximum).
    const auto mixIndex = m_session.getSelectedMixIndex();
    const auto& mix = song->getMix (mixIndex);

    int insertRows = InsertSlots::rowsFor (mix.getMaster().getInserts().size());

    for (const auto& track : song->getTracks())
        insertRows = juce::jmax (insertRows, InsertSlots::rowsFor (mix.getChannel (track.getId()).getInserts().size()));

    for (auto& strip : m_strips)
    {
        if (strip->insertRows != insertRows)
        {
            strip->insertRows = insertRows;
            strip->resized();
        }

        if (strip->inserts != nullptr)
            strip->inserts->repaint();
    }

    for (auto& strip : m_strips)
    {
        if (strip->trackId == Strip::masterId)
        {
            if (! strip->fader.getFader().isMouseButtonDown())
                strip->fader.setVolume (mix.getMaster().getVolume(), juce::dontSendNotification);

            if (! strip->balance.isMouseButtonDown())
                strip->balance.setValue (mix.getMaster().getBalance(), juce::dontSendNotification);

            strip->output.setButtonText (outputsText (m_session, mix.getMaster()));
            strip->output.setTooltip ("Where " + mix.getName() + " goes. No two mixes share an output: picking one another mix uses sets that mix to N/A.");
            strip->name = mix.getName();
            strip->repaint();
            strip->output.setEnabled (! m_session.isEditLocked());   // routing applies from the next play
            continue;
        }

        if (strip->trackId == Strip::clickId)
        {
            if (! strip->fader.getFader().isMouseButtonDown())
                strip->fader.setVolume (song->getMetronome().getVolume(), juce::dontSendNotification);

            // Shown in every mix; dimmed where the click is off.
            const auto on = song->getMetronome().playsIn (mixIndex);
            strip->clickOn.setToggleState (on, juce::dontSendNotification);
            strip->clickOn.setButtonText (on ? "On" : "Off");
            strip->fader.setAlpha (on ? 1.0f : 0.45f);
            strip->name = "Metronome";
            strip->repaint();
            continue;
        }

        if (auto* track = song->getTrack (strip->trackId))
        {
            const auto& channel = mix.getChannel (track->getId());

            if (! strip->fader.getFader().isMouseButtonDown())
                strip->fader.setVolume (channel.getVolume(), juce::dontSendNotification);

            if (! strip->balance.isMouseButtonDown())
                strip->balance.setValue (channel.getBalance(), juce::dontSendNotification);

            strip->mute.setToggleState (channel.isMuted(), juce::dontSendNotification);
            strip->solo.setToggleState (channel.isSoloed(), juce::dontSendNotification);

            strip->name = juce::String (track->getName());
            strip->plateColour = track->getTrackType() == TrackType::Wav ? juce::Colour (0xff3b7fc0) : juce::Colour (0xffc0675f);
            strip->selected = track->getId() == m_session.getSelectedTrackId();
            strip->repaint();
        }
    }
}

void MixerPanel::rebuildStrips()
{
    m_strips.clear();
    m_stripTrackIds.clear();

    auto* song = m_session.getSelectedSong();

    if (song != nullptr)
    {
        for (const auto& track : song->getTracks())
        {
            auto strip = std::make_unique<Strip> (m_session, track.getId(), juce::Colours::grey);
            const auto trackId = track.getId();

            strip->fader.onVolumeChange = [this, trackId] (float decibels) { m_session.setTrackVolume (trackId, decibels); };
            strip->fader.getFader().onDragStart = [this, trackId] { m_session.selectTrack (trackId); };

            auto* stripPtr = strip.get();
            strip->mute.onClick = [this, trackId, stripPtr] { m_session.setTrackMute (trackId, stripPtr->mute.getToggleState()); };
            strip->solo.onClick = [this, trackId, stripPtr] { m_session.setTrackSolo (trackId, stripPtr->solo.getToggleState()); };
            strip->balance.onValueChange = [this, trackId, stripPtr] { m_session.setTrackBalance (trackId, (int) stripPtr->balance.getValue()); };
            m_stripTrackIds.push_back (trackId);
            m_strips.push_back (std::move (strip));
        }

        // The metronome level changes live, like a track's (smoothed by the engine). Its strip is
        // hidden while the metronome is off; each mix switches the click on or off for itself.
        if (showsClick (*song))
        {
            auto click = std::make_unique<Strip> (m_session, Strip::clickId, juce::Colour (0xff8a6fc9));
            auto* clickPtr = click.get();
            click->name = "Metronome";
            click->clickOn.onClick = [this, clickPtr]
            {
                m_session.setClickInMix (m_session.getSelectedMixIndex(), clickPtr->clickOn.getToggleState());
            };
            click->fader.onVolumeChange = [this, clickPtr] (float decibels)
            {
                const auto volume = juce::jmax (MBoxMetronome::minVolume, juce::jmin (MBoxMetronome::maxVolume, decibels));

                if (m_session.editMetronome ([volume] (MBoxSong& s) { return s.getMetronome().setVolume (volume); },
                                             "Metronome volume " + UIHelpers::volumeText (volume)).failed())
                    clickPtr->fader.setVolume (m_session.getSelectedSong()->getMetronome().getVolume(), juce::dontSendNotification);
            };
            m_stripTrackIds.push_back (Strip::clickId);
            m_strips.push_back (std::move (click));
        }

        // Master: the volume is live; the outputs it applies to are picked from a menu.
        auto master = std::make_unique<Strip> (m_session, Strip::masterId, juce::Colour (0xff7d8796));
        auto* masterPtr = master.get();
        master->name = "Master";
        master->fader.onVolumeChange = [this] (float decibels) { m_session.setMasterVolume (decibels); };
        master->balance.onValueChange = [this, masterPtr] { m_session.setMasterBalance ((int) masterPtr->balance.getValue()); };
        master->output.onClick = [this, masterPtr]
        {
            auto* current = m_session.getSelectedSong();

            if (current == nullptr)
                return;

            auto* engine = m_session.getApp().getAudioEngine();
            const auto names = engine != nullptr ? engine->getOutputChannelNames() : juce::StringArray();

            const auto mix = m_session.getSelectedMixIndex();

            ListEditorPanel::outputsMenu (names, current->getMix (mix).getMaster().getOutputs(), true)
                .showMenuAsync (juce::PopupMenu::Options().withTargetComponent (masterPtr->output),
                                [this, mix] (int id)
                                {
                                    if (id == 0)
                                        return;

                                    const auto outputs = ListEditorPanel::outputsForId (id);
                                    m_session.setMixOutputs (mix, outputs, ListEditorPanel::outputsText (outputs));
                                });
        };
        m_stripTrackIds.push_back (Strip::masterId);
        m_strips.push_back (std::move (master));
    }

    for (auto& strip : m_strips)
        m_content.addAndMakeVisible (*strip);

    layoutStrips();
}

bool MixerPanel::showsClick (const MBoxSong& song) const
{
    return song.isMetronomeActive();
}

juce::String MixerPanel::outputsText (Session& session, const MBoxMaster& master)
{
    // Outputs the audio device doesn't have play nowhere: show them as N/A, like no outputs at all.
    auto* engine = session.getApp().getAudioEngine();

    if (master.hasOutputs() && engine != nullptr && engine->isInitialised())
    {
        const auto numOutputs = engine->getOutputChannelNames().size();
        const auto channels = MBoxMaster::channelsOf (master.getOutputs());

        if (std::any_of (channels.begin(), channels.end(), [numOutputs] (int c) { return c > numOutputs; }))
            return "N/A";
    }

    return ListEditorPanel::outputsText (master.getOutputs());
}

void MixerPanel::timerCallback()
{
    auto* engine = m_session.getApp().getAudioEngine();

    for (auto& strip : m_strips)
    {
        float peak = 0.0f;

        // Monitored inputs are heard (and metered) while stopped too.
        if (engine != nullptr && (engine->isActive() || engine->isMonitoring()))
        {
            const auto mix = m_session.getSelectedMixIndex();
            // The click's meter only moves in the mixes it plays in.
            const auto* song = m_session.getSelectedSong();
            const auto clickHere = song != nullptr && song->getMetronome().playsIn (mix);
            peak = strip->trackId == Strip::clickId    ? (clickHere ? engine->takeMetronomePeak() : 0.0f)
                 : strip->trackId == Strip::masterId ? engine->takeMasterPeak (mix)
                                                     : engine->takeTrackPeak (mix, strip->trackId);
        }

        strip->updateMeter (peak);
    }
}
