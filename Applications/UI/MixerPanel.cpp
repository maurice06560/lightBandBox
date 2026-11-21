#include "MixerPanel.h"
#include "ListEditorPanel.h"
#include "PluginMenu.h"

namespace
{
    // Rows the insert area shows at most (the chains themselves have no limit)
    constexpr int maxInsertRows = 4;

    // Balance knobs show -1..+1 as -100..+100
    constexpr double balanceScale = 100.0;
}

/** A strip's insert plugins as a column of pills, in processing order, then an "Add" pill while
    there is room. Clicking a plugin opens its window (during playback too, heard live); its ×
    (shown on hover) or its right-click menu removes it, and the menu bypasses it. Adding and
    removing are refused while a song plays or is paused.
*/
class MixerPanel::InsertSlots final : public juce::Component,
                                      public juce::TooltipClient
{
public:
    static constexpr int rowHeight = 18;
    static constexpr int rowGap = 2;

    InsertSlots (Session& session, PluginWindows& windows, int stripId)
        : m_session (session), m_windows (windows), m_stripId (stripId) {}

    std::function<juce::String()> stripName;

    void paint (juce::Graphics& g) override
    {
        const auto* inserts = m_session.getInserts (m_stripId);

        if (inserts == nullptr)
            return;

        const auto& palette = ui::getTheme (*this).getPalette();
        const auto locked = m_session.isEditLocked();
        const auto count = (int) inserts->size();
        g.setFont (ui::LookAndFeel::makeFont (10.0f, true));

        for (int row = 0; row < rowsFor (count); ++row)
        {
            const auto area = rowBounds (row).toFloat();

            if (row == count)
            {
                // The Add pill: an outline while editing is possible
                const auto colour = locked ? palette.textDim.withAlpha (0.4f) : palette.textDim;
                g.setColour (colour.withAlpha (row == m_hoverRow && ! locked ? 0.35f : 0.15f));
                g.fillRoundedRectangle (area, area.getHeight() / 2.0f);
                g.setColour (colour);
                g.drawRoundedRectangle (area.reduced (0.5f), area.getHeight() / 2.0f, 1.0f);
                g.setColour (locked ? palette.textDim.withAlpha (0.5f) : palette.text);
                g.drawText ("Add", area, juce::Justification::centred, false);
                continue;
            }

            const auto& effect = (*inserts)[(size_t) row];
            const auto base = effect.m_bypassed ? palette.textDim : palette.accent;
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

            g.setColour (juce::Colours::white.withAlpha (0.95f));
            g.drawText (effect.m_name, text, juce::Justification::centred, true);
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

        if (row == (int) inserts->size())
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

        if (m_hoverRow == (int) inserts->size())
            return "Add a plugin to this strip";

        const auto& effect = (*inserts)[(size_t) m_hoverRow];
        return effect.m_name + " by " + effect.m_manufacturer + " (" + PluginMenu::formatLabel (effect.m_format) + ")"
             + (effect.m_bypassed ? ", bypassed" : "") + ". Click to open; right-click for more; x to remove.";
    }

    /** How many rows a strip with this many inserts shows: the plugins and the Add pill. */
    static int rowsFor (int numInserts) { return juce::jmin (numInserts + 1, maxInsertRows); }
    static int heightFor (int rows)     { return rows * (rowHeight + rowGap); }

private:
    /** The Add pill sits directly above the fader, the plugins stacked above it in processing
        order, so every strip's Add pill is on the same line.
    */
    juce::Rectangle<int> rowBounds (int row) const
    {
        const auto* inserts = m_session.getInserts (m_stripId);
        const auto shown = rowsFor (inserts != nullptr ? (int) inserts->size() : 0);
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

        for (int row = 0; row < rowsFor ((int) inserts->size()); ++row)
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

    MBoxEffectTarget target() const
    {
        const auto bus = m_session.getSelectedBus();
        return m_stripId == Session::masterStrip ? PluginWindows::masterTarget (bus) : PluginWindows::insertTarget (bus, m_stripId);
    }

    juce::String title (int row) const
    {
        const auto* inserts = m_session.getInserts (m_stripId);
        const auto name = inserts != nullptr && row < (int) inserts->size() ? (*inserts)[(size_t) row].m_name : juce::String();
        return name + " - " + (stripName != nullptr ? stripName() : juce::String()) + " (" + m_session.getSelectedMixBus().getName() + ")";
    }

    void showAddMenu()
    {
        if (! m_session.canEdit())
            return;

        PluginMenu::show (m_session, *this, [&session = m_session, &windows = m_windows, stripId = m_stripId,
                                             strip = stripName != nullptr ? stripName() : juce::String(),
                                             effectTarget = target()] (const juce::PluginDescription& description)
                          {
                              // The edit refreshes every panel, which may rebuild this component: only captured values here
                              int index = -1;
                              const auto result = session.editInserts (stripId,
                                                                       [&] (std::vector<MBoxEffect>& chain)
                                                                       {
                                                                           chain.push_back (MBoxEffect::fromDescription (description));
                                                                           index = (int) chain.size() - 1;
                                                                           return juce::Result::ok();
                                                                       },
                                                                       description.name + " added to " + strip + ": it opens once the song has reloaded");

                              if (result.wasOk())
                                  windows.openWhenRunning (effectTarget, index, description.name + " - " + strip);
                          });
    }

    void openWindow (int row)
    {
        m_windows.open (target(), row, title (row));
    }

    void showInsertMenu (int row)
    {
        const auto* inserts = m_session.getInserts (m_stripId);

        if (inserts == nullptr || row >= (int) inserts->size())
            return;

        const auto bypassed = (*inserts)[(size_t) row].m_bypassed;
        juce::PopupMenu menu;
        menu.addItem (1, "Open plugin window");
        menu.addItem (2, "Bypass", m_session.isSelectionLoaded(), bypassed);
        menu.addSeparator();
        menu.addItem (3, "Remove", ! m_session.isEditLocked());

        menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this),
                            [safe = juce::Component::SafePointer<InsertSlots> (this), row, bypassed] (int id)
                            {
                                if (safe == nullptr)
                                    return;

                                if (id == 1)
                                    safe->openWindow (row);
                                else if (id == 2)
                                    safe->setBypassed (row, ! bypassed);
                                else if (id == 3)
                                    safe->remove (row);
                            });
    }

    void setBypassed (int row, bool bypassed)
    {
        const auto result = m_session.getCore().setEffectBypass (target(), row, bypassed);
        m_session.report (result.wasOk() ? juce::Result::ok() : juce::Result::fail (juce::String (result.m_message)),
                          (bypassed ? "Bypassed " : "Back on: ") + title (row));
        m_session.changed();
    }

    void remove (int row)
    {
        auto* inserts = m_session.getInserts (m_stripId);

        if (inserts == nullptr || row >= (int) inserts->size())
            return;

        const auto name = (*inserts)[(size_t) row].m_name;
        const auto strip = stripName != nullptr ? stripName() : juce::String();

        // The effects after it move up: their windows close
        m_windows.close (target());

        // The edit refreshes every panel, which may rebuild this component: nothing after it
        m_session.editInserts (m_stripId,
                               [row] (std::vector<MBoxEffect>& chain)
                               {
                                   chain.erase (chain.begin() + row);
                                   return juce::Result::ok();
                               },
                               name + " removed from " + strip);
    }

    Session& m_session;
    PluginWindows& m_windows;
    const int m_stripId;
    int m_hoverRow = -1;
    bool m_hoverRemove = false;
};

//==============================================================================
/** A channel strip: inserts, balance, fader with meter, and a coloured name plate. */
struct MixerPanel::Strip final : public juce::Component
{
    Strip (Session& session, PluginWindows& windows, int id, juce::Colour colour) : trackId (id), plateColour (colour)
    {
        addAndMakeVisible (fader);

        // Every strip but the Metronome's has inserts
        if (trackId != clickId)
        {
            inserts = std::make_unique<InsertSlots> (session, windows, trackId == masterId ? Session::masterStrip : trackId);
            inserts->stripName = [this] { return name; };
            addAndMakeVisible (*inserts);
        }

        balance.setRange (-balanceScale, balanceScale, 1.0);
        balance.setNumDecimalPlacesToDisplay (0);
        balance.setTooltip ("Balance: -100 (left) to +100 (right)");

        if (trackId == masterId)
        {
            output.setTooltip ("The outputs this mix goes to");
            addAndMakeVisible (output);
            addAndMakeVisible (balance);
        }
        else if (trackId == clickId)
        {
            clickOn.setTooltip ("Whether the click plays in this mix (for every song)");
            addAndMakeVisible (clickOn);
        }
        else
        {
            mute.setTooltip ("Mute");
            solo.setTooltip ("Solo: only soloed tracks play");
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

        // Every strip reserves the same height so that the faders line up
        auto insertArea = area.removeFromTop (InsertSlots::heightFor (insertRows));
        area.removeFromTop (2);

        if (inserts != nullptr)
            inserts->setBounds (insertArea);

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
MixerPanel::MixerPanel (Session& session, PluginWindows& pluginWindows)
    : ui::Panel ("Mixer"), m_session (session), m_pluginWindows (pluginWindows)
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
    // Narrow strips, like Logic's mixer: the channel fader switches to its compact layout
    constexpr int stripWidth = 60;
    constexpr int stripGap = 3;
    constexpr int clickGap = 12;   // sets the Metronome and Master strips apart, like Logic's output strips
    const auto height = m_viewport.getHeight() - (m_viewport.isHorizontalScrollBarShown() ? 10 : 0);
    m_content.setSize (juce::jmax (m_viewport.getWidth(), (int) m_strips.size() * (stripWidth + stripGap) + 2 * clickGap), juce::jmax (0, height));

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
            ids.push_back (track->getId());

        if (showsClick (*song))
            ids.push_back (Strip::clickId);

        ids.push_back (Strip::masterId);
    }

    if (ids != m_stripTrackIds)
        rebuildStrips();

    if (song == nullptr)
        return;

    const auto bus = m_session.getSelectedBus();
    auto& mixBus = m_session.getSelectedMixBus();

    // The insert area fits the busiest strip (one row more for its Add pill, up to the maximum)
    int insertRows = InsertSlots::rowsFor ((int) mixBus.getMasterEffectChain().size());

    for (const auto& trackMix : song->getMix (bus))
        insertRows = juce::jmax (insertRows, InsertSlots::rowsFor ((int) trackMix.m_inserts.size()));

    const auto locked = m_session.isEditLocked();

    for (auto& strip : m_strips)
    {
        if (strip->insertRows != insertRows)
        {
            strip->insertRows = insertRows;
            strip->resized();
        }

        if (strip->inserts != nullptr)
            strip->inserts->repaint();

        if (strip->trackId == Strip::masterId)
        {
            if (! strip->fader.getFader().isMouseButtonDown())
                strip->fader.setVolume (UIHelpers::toFaderDb (mixBus.getMasterVolume()), juce::dontSendNotification);

            if (! strip->balance.isMouseButtonDown())
                strip->balance.setValue (mixBus.getMasterBalance() * balanceScale, juce::dontSendNotification);

            strip->output.setButtonText (outputsText (m_session, mixBus.getOutputs()));
            strip->output.setTooltip ("Where " + mixBus.getName() + " goes (for every song)");
            strip->output.setEnabled (! locked);
            strip->name = mixBus.getName();
            strip->repaint();
            continue;
        }

        if (strip->trackId == Strip::clickId)
        {
            if (! strip->fader.getFader().isMouseButtonDown())
                strip->fader.setVolume (UIHelpers::toFaderDb (mixBus.getMetronomeVolume()), juce::dontSendNotification);

            // Dimmed where the click is off
            const auto on = mixBus.isMetronomeOn();
            strip->clickOn.setToggleState (on, juce::dontSendNotification);
            strip->clickOn.setButtonText (on ? "On" : "Off");
            strip->fader.setAlpha (on ? 1.0f : 0.45f);
            strip->name = "Metronome";
            strip->repaint();
            continue;
        }

        if (auto* track = song->getTrack (strip->trackId))
        {
            const auto* trackMix = song->getTrackMix (bus, track->getId());

            if (trackMix == nullptr)
                continue;

            if (! strip->fader.getFader().isMouseButtonDown())
                strip->fader.setVolume (UIHelpers::toFaderDb (trackMix->m_volume), juce::dontSendNotification);

            if (! strip->balance.isMouseButtonDown())
                strip->balance.setValue (trackMix->m_balance * balanceScale, juce::dontSendNotification);

            strip->mute.setToggleState (trackMix->m_mute, juce::dontSendNotification);
            strip->solo.setToggleState (trackMix->m_solo, juce::dontSendNotification);

            strip->name = track->getName();
            strip->plateColour = track->getType() == TrackType::Audio    ? juce::Colour (0xff3b7fc0)
                               : track->getType() == TrackType::Physical ? juce::Colour (0xffc0675f)
                                                                         : juce::Colour (0xff8a6fc9);
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
            auto strip = std::make_unique<Strip> (m_session, m_pluginWindows, track->getId(), juce::Colours::grey);
            const auto trackId = track->getId();
            auto* stripPtr = strip.get();

            strip->fader.onVolumeChange = [this, trackId] (float decibels) { m_session.setTrackVolume (trackId, UIHelpers::fromFaderDb (decibels)); };
            strip->fader.getFader().onDragStart = [this, trackId] { m_session.selectTrack (trackId); };
            strip->mute.onClick = [this, trackId, stripPtr] { m_session.setTrackMute (trackId, stripPtr->mute.getToggleState()); };
            strip->solo.onClick = [this, trackId, stripPtr] { m_session.setTrackSolo (trackId, stripPtr->solo.getToggleState()); };
            strip->balance.onValueChange = [this, trackId, stripPtr]
            {
                m_session.setTrackBalance (trackId, stripPtr->balance.getValue() / balanceScale);
            };

            m_stripTrackIds.push_back (trackId);
            m_strips.push_back (std::move (strip));
        }

        // The metronome's level in this bus, live (rack-wide, like the bus)
        if (showsClick (*song))
        {
            auto click = std::make_unique<Strip> (m_session, m_pluginWindows, Strip::clickId, juce::Colour (0xff8a6fc9));
            auto* clickPtr = click.get();
            click->name = "Metronome";
            click->clickOn.onClick = [this, clickPtr] { m_session.setMetronomeInMix (clickPtr->clickOn.getToggleState()); };
            click->fader.onVolumeChange = [this] (float decibels) { m_session.setMetronomeVolume (UIHelpers::fromFaderDb (decibels)); };
            m_stripTrackIds.push_back (Strip::clickId);
            m_strips.push_back (std::move (click));
        }

        // Master: volume and balance are live; the outputs are picked from a menu, while stopped
        auto master = std::make_unique<Strip> (m_session, m_pluginWindows, Strip::masterId, juce::Colour (0xff7d8796));
        auto* masterPtr = master.get();
        master->name = "Master";
        master->fader.onVolumeChange = [this] (float decibels) { m_session.setMasterVolume (UIHelpers::fromFaderDb (decibels)); };
        master->balance.onValueChange = [this, masterPtr] { m_session.setMasterBalance (masterPtr->balance.getValue() / balanceScale); };
        master->output.onClick = [this, masterPtr]
        {
            const auto device = m_session.getCore().getCurrentDevice();
            const auto bus = m_session.getSelectedBus();

            ListEditorPanel::outputsMenu (device.m_outputNames, m_session.getSelectedMixBus().getOutputs())
                .showMenuAsync (juce::PopupMenu::Options().withTargetComponent (masterPtr->output),
                                [this, bus] (int id)
                                {
                                    if (id == 0)
                                        return;

                                    const auto outputs = ListEditorPanel::outputsForId (id);
                                    m_session.editRig ([bus, outputs] (MBoxRig& rig)
                                                       {
                                                           return rig.m_mixBuses[(size_t) bus - 1].setOutputs (outputs)
                                                                      ? juce::Result::ok() : juce::Result::fail ("Invalid outputs");
                                                       },
                                                       m_session.getRig().m_mixBuses[(size_t) bus - 1].getName() + " goes to "
                                                           + ListEditorPanel::outputsText (outputs));
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
    return song.getMetronomeState() != MetronomeState::Off;
}

juce::String MixerPanel::outputsText (Session& session, std::pair<int, int> outputs)
{
    // Outputs the audio device doesn't have play nowhere: show them as N/A, like no outputs at all
    const auto numOutputs = session.getCore().getCurrentDevice().m_numOutputs;

    if (numOutputs > 0 && (outputs.first >= numOutputs || outputs.second >= numOutputs))
        return "N/A";

    return ListEditorPanel::outputsText (outputs);
}

void MixerPanel::timerCallback()
{
    auto& core = m_session.getCore();
    const auto bus = m_session.getSelectedBus();
    const auto live = m_session.isSelectionLoaded();

    for (auto& strip : m_strips)
    {
        float peak = 0.0f;

        if (live)
            peak = strip->trackId == Strip::clickId  ? core.takeMetronomePeak (bus)
                 : strip->trackId == Strip::masterId ? core.takeMasterPeak (bus)
                                                     : core.takeTrackPeak (bus, strip->trackId);

        strip->updateMeter (peak);
    }
}
