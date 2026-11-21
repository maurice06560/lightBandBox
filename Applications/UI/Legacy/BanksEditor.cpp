#include "BanksEditor.h"
#include "../Audio/MBoxAudioEngine.h"
#include "../Effects/AudioEffectFactory.h"
#include "PluginMenu.h"

namespace
{
    using PluginMenu::formatLabel;

    /** The chain entry's label: the effect name, plus the format for a plugin and a warning if it is missing. */
    juce::String effectLabel (const IAudioEffect& effect)
    {
        if (const auto* plugin = dynamic_cast<const MAudioEffect*> (&effect))
            return plugin->getName() + "  (" + formatLabel (plugin->getDescription()) + (plugin->isMissing() ? ", not found)" : ")");

        return effect.getName();
    }

    MAudioEffect* asPlugin (IAudioEffect* effect) { return dynamic_cast<MAudioEffect*> (effect); }
}

/** Banks with their slots underneath, painted like the playlist browser. */
class BanksEditor::SlotList final : public juce::Component
{
public:
    explicit SlotList (BanksEditor& owner) : m_owner (owner) {}

    static constexpr int bankRowHeight = 28;
    static constexpr int slotRowHeight = 26;

    void paint (juce::Graphics& g) override
    {
        const auto& palette = ui::getTheme (*this).getPalette();
        const auto& banks = m_owner.m_session.getApp().getBanks();
        int y = 2;

        for (int b = 0; b < MBoxApplication::numBanks; ++b)
        {
            const auto colour = UIHelpers::bankColour (b);
            auto header = juce::Rectangle<float> (2.0f, (float) y, (float) getWidth() - 4.0f, (float) bankRowHeight - 2.0f);
            g.setColour (colour.withAlpha (0.18f));
            g.fillRoundedRectangle (header, 4.0f);
            g.setColour (colour);
            g.fillRoundedRectangle (header.withWidth (4.0f), 2.0f);
            g.setColour (palette.text.brighter (0.3f));
            g.setFont (ui::LookAndFeel::makeFont (13.0f, true));
            g.drawText (banks[(size_t) b].m_name, header.reduced (12.0f, 0.0f), juce::Justification::centredLeft, true);
            y += bankRowHeight;

            for (int s = 0; s < MBoxBank::numSlots; ++s)
            {
                const auto& slot = banks[(size_t) b].m_bankSlots[(size_t) s];
                auto row = juce::Rectangle<float> (2.0f, (float) y, (float) getWidth() - 4.0f, (float) slotRowHeight - 2.0f);
                const auto selected = m_owner.m_selected == BankSlotRef { b, s };

                if (selected)
                {
                    g.setColour (palette.accent.withAlpha (0.85f));
                    g.fillRoundedRectangle (row, 4.0f);
                }

                g.setColour (selected ? palette.textOnAccent : palette.text);
                g.setFont (ui::LookAndFeel::makeFont (12.5f));
                g.drawText (slot.m_name, row.reduced (22.0f, 0.0f).withTrimmedRight (slot.m_loudness.has_value() ? 110.0f : 30.0f),
                            juce::Justification::centredLeft, true);

                // The loudness measured through the slot, left of the effect count.
                if (slot.m_loudness.has_value())
                {
                    g.setColour (selected ? palette.textOnAccent : palette.solo.withAlpha (0.9f));
                    g.setFont (ui::LookAndFeel::makeFont (11.0f, true));
                    g.drawText (juce::String (*slot.m_loudness, 1) + " LUFS", row.reduced (10.0f, 0.0f).withTrimmedRight (36.0f),
                                juce::Justification::centredRight, false);
                }

                g.setColour (selected ? palette.textOnAccent.withAlpha (0.7f) : palette.textDim.brighter (0.2f));
                g.setFont (ui::LookAndFeel::makeFont (11.0f));
                g.drawText (slot.m_effectChain.empty() ? juce::String ("dry") : juce::String ((int) slot.m_effectChain.size()) + " fx",
                            row.reduced (10.0f, 0.0f), juce::Justification::centredRight, false);
                y += slotRowHeight;
            }
        }
    }

    int totalHeight() const { return 4 + MBoxApplication::numBanks * (bankRowHeight + MBoxBank::numSlots * slotRowHeight); }

    void mouseDown (const juce::MouseEvent& e) override
    {
        int y = 2;

        for (int b = 0; b < MBoxApplication::numBanks; ++b)
        {
            y += bankRowHeight;

            for (int s = 0; s < MBoxBank::numSlots; ++s)
            {
                if (e.y >= y && e.y < y + slotRowHeight)
                {
                    m_owner.selectSlot ({ b, s });
                    return;
                }

                y += slotRowHeight;
            }
        }
    }

private:
    BanksEditor& m_owner;
};

/** The selected slot's effects, in processing order. */
class BanksEditor::ChainList final : public juce::Component
{
public:
    explicit ChainList (BanksEditor& owner) : m_owner (owner) {}

    static constexpr int rowHeight = 34;

    void paint (juce::Graphics& g) override
    {
        const auto& palette = ui::getTheme (*this).getPalette();
        auto& chain = m_owner.slot().m_effectChain;

        g.setColour (palette.arcTrack.withAlpha (0.7f));
        g.fillRoundedRectangle (getLocalBounds().toFloat(), 6.0f);

        if (chain.empty())
        {
            g.setColour (palette.textDim.brighter (0.2f));
            g.setFont (ui::LookAndFeel::makeFont (12.5f));
            g.drawText ("Dry: no effects. Add one with the buttons below.", getLocalBounds(), juce::Justification::centred, false);
            return;
        }

        int index = 0;

        for (auto& instance : chain)
        {
            auto row = juce::Rectangle<float> (4.0f, 4.0f + (float) (index * rowHeight), (float) getWidth() - 8.0f, (float) rowHeight - 4.0f);
            const auto selected = index == m_owner.m_selectedEffect;

            g.setGradientFill (juce::ColourGradient (selected ? palette.accent : palette.capTop, 0.0f, row.getY(),
                                                     selected ? palette.accent.darker (0.3f) : palette.capBottom, 0.0f, row.getBottom(), false));
            g.fillRoundedRectangle (row, 5.0f);

            g.setColour (selected ? palette.textOnAccent : palette.text.brighter (0.3f));
            g.setFont (ui::LookAndFeel::makeFont (13.0f, true));
            g.drawText (juce::String (index + 1) + ".  " + effectLabel (instance.getEffect()), row.reduced (12.0f, 0.0f),
                        juce::Justification::centredLeft, false);
            ++index;
        }
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        const auto index = (e.y - 4) / rowHeight;

        if (index >= 0 && index < (int) m_owner.slot().m_effectChain.size())
        {
            m_owner.m_selectedEffect = index;
            m_owner.rebuildKnobs();
            repaint();
        }
    }

    void mouseDoubleClick (const juce::MouseEvent& e) override
    {
        // Double-clicking a plugin opens its window.
        if (auto* plugin = asPlugin (m_owner.selectedEffect()); plugin != nullptr && (e.y - 4) / rowHeight == m_owner.m_selectedEffect
                                                                 && ! m_owner.m_session.isEditLocked())
            m_owner.openPluginWindow (*plugin);
    }

private:
    BanksEditor& m_owner;
};

//==============================================================================
BanksEditor::BanksEditor (Session& session) : m_session (session)
{
    m_slotList = std::make_unique<SlotList> (*this);
    m_chainList = std::make_unique<ChainList> (*this);

    for (auto* c : std::initializer_list<juce::Component*> { &m_slotsPanel, &m_slotPanel, &m_paramsPanel })
        addAndMakeVisible (c);

    m_slotViewport.setViewedComponent (m_slotList.get(), false);
    m_slotViewport.setScrollBarsShown (true, false);
    m_slotViewport.setScrollBarThickness (8);
    m_slotsPanel.addAndMakeVisible (m_slotViewport);

    for (auto* c : std::initializer_list<juce::Component*> { &m_title, &m_slotNameCaption, &m_slotName, &m_bankNameCaption, &m_bankName,
                                                              &m_chainHeading, m_chainList.get(), &m_up, &m_down, &m_remove, &m_addPlugin })
        m_slotPanel.addAndMakeVisible (c);

    for (const auto& type : AudioEffectFactory::getAvailableTypes())
    {
        auto* button = m_addButtons.add (new ui::Button ("+ " + type, ui::Button::Variant::Accent));
        button->onClick = [this, type] { addEffect (type); };
        m_slotPanel.addAndMakeVisible (button);
    }

    m_paramsPanel.addAndMakeVisible (m_paramsHint);
    m_paramsPanel.addChildComponent (m_openPlugin);

    m_addPlugin.onClick = [this] { showPluginMenu(); };
    m_openPlugin.onClick = [this]
    {
        if (auto* plugin = asPlugin (selectedEffect()))
            openPluginWindow (*plugin);
    };

    m_slotName.setEditable (false, true);
    m_slotName.onTextChange = [this]
    {
        const auto name = m_slotName.getText().trim();
        const auto ref = m_selected;
        m_session.editBanks ([&] (MBoxApplication::Banks& banks)
                             {
                                 if (name.isEmpty())
                                     return juce::Result::fail ("A slot needs a name");

                                 banks[(size_t) ref.bank].m_bankSlots[(size_t) ref.slot].m_name = name.toStdString();
                                 return juce::Result::ok();
                             },
                             "Slot renamed to " + name);
    };

    m_bankName.setEditable (false, true);
    m_bankName.onTextChange = [this]
    {
        const auto name = m_bankName.getText().trim();
        const auto bank = m_selected.bank;
        m_session.editBanks ([&] (MBoxApplication::Banks& banks)
                             {
                                 if (name.isEmpty())
                                     return juce::Result::fail ("A bank needs a name");

                                 banks[(size_t) bank].m_name = name.toStdString();
                                 return juce::Result::ok();
                             },
                             "Bank renamed to " + name);
    };

    m_up.onClick = [this] { moveEffect (-1); };
    m_down.onClick = [this] { moveEffect (1); };
    m_remove.onClick = [this] { removeEffect(); };

    // Loudness: input menu, meter, recorded value and reset.
    for (auto* c : std::initializer_list<juce::Component*> { &m_meter, &m_loudnessValue, &m_input, &m_resetLoudness })
        m_slotPanel.addAndMakeVisible (c);

    m_input.setTooltip ("Plays this input through the slot, to the outputs of the mix selected in the main window, and measures its loudness");
    m_input.onChange = [this] { inputChosen(); };
    m_loudnessValue.setTooltip ("Integrated loudness (LUFS) measured through this slot, saved with the banks");
    m_meter.setTooltip ("Loudness: bar = momentary (400 ms), line = short-term (3 s), yellow = recorded integrated value");
    m_resetLoudness.setTooltip ("Clears this slot's recorded loudness");
    m_resetLoudness.onClick = [this] { resetLoudness(); };
    refreshInputMenu();

    m_session.addListener (this);
    selectSlot ({ 0, 0 });
    setSize (960, 640);
    startTimerHz (30);
}

BanksEditor::~BanksEditor()
{
    stopTimer();
    stopMonitoring();
    m_session.removeListener (this);
}

MBoxBankSlot& BanksEditor::slot()
{
    return m_session.getApp().getBanks()[(size_t) m_selected.bank].m_bankSlots[(size_t) m_selected.slot];
}

IAudioEffect* BanksEditor::selectedEffect()
{
    auto& chain = slot().m_effectChain;

    if (m_selectedEffect < 0 || m_selectedEffect >= (int) chain.size())
        return nullptr;

    return &std::next (chain.begin(), m_selectedEffect)->getEffect();
}

void BanksEditor::selectSlot (BankSlotRef ref)
{
    m_selected = ref;
    m_selectedEffect = slot().m_effectChain.empty() ? -1 : 0;
    sessionChanged();
}

void BanksEditor::sessionChanged()
{
    auto& current = slot();
    m_title.setText (m_selected.toString(), juce::dontSendNotification);
    m_slotName.setText (current.m_name, juce::dontSendNotification);
    m_bankName.setText (m_session.getApp().getBanks()[(size_t) m_selected.bank].m_name, juce::dontSendNotification);

    m_selectedEffect = juce::jmin (m_selectedEffect, (int) current.m_effectChain.size() - 1);

    if (m_selectedEffect < 0 && ! current.m_effectChain.empty())
        m_selectedEffect = 0;

    // Banks can't change while a song plays or is paused: browse only, everything else greyed.
    const auto unlocked = ! m_session.isEditLocked();
    const auto hasEffect = m_selectedEffect >= 0;
    m_up.setEnabled (unlocked && hasEffect && m_selectedEffect > 0);
    m_down.setEnabled (unlocked && hasEffect && m_selectedEffect < (int) current.m_effectChain.size() - 1);
    m_remove.setEnabled (unlocked && hasEffect);
    m_slotName.setEnabled (unlocked);
    m_bankName.setEnabled (unlocked);

    for (auto* button : m_addButtons)
        button->setEnabled (unlocked);

    m_addPlugin.setEnabled (unlocked);

    if (auto* plugin = asPlugin (selectedEffect()))
        m_openPlugin.setEnabled (unlocked && ! plugin->isMissing());

    if (! unlocked)
        closePluginWindows();

    refreshKnobs();

    for (auto* knob : m_knobs)
        knob->setEnabled (unlocked);

    // Monitoring needs the song stopped (the timer ends a running one).
    m_input.setEnabled (unlocked);
    updateLoudnessDisplay();

    m_slotList->repaint();
    m_chainList->repaint();
}

void BanksEditor::refreshKnobs()
{
    auto* effect = selectedEffect();
    const auto key = effect == nullptr ? juce::String()
                                       : juce::String (m_selected.bank) + "/" + juce::String (m_selected.slot) + "/"
                                             + juce::String (m_selectedEffect) + "/" + effect->getName();

    if (key != m_knobsFor || effect == nullptr || (int) effect->getParameters().size() != m_knobs.size())
    {
        rebuildKnobs();
        return;
    }

    for (size_t i = 0; i < effect->getParameters().size(); ++i)
        if (! m_knobs[(int) i]->isMouseButtonDown())
            m_knobs[(int) i]->setValue (effect->getParameters()[i].value, juce::dontSendNotification);
}

void BanksEditor::rebuildKnobs()
{
    m_knobs.clear();
    auto* effect = selectedEffect();
    m_knobsFor = effect == nullptr ? juce::String()
                                   : juce::String (m_selected.bank) + "/" + juce::String (m_selected.slot) + "/"
                                         + juce::String (m_selectedEffect) + "/" + effect->getName();

    m_paramsPanel.setTitle (effect != nullptr ? effect->getName() + " parameters" : juce::String ("Parameters"));
    m_paramsHint.setText (effect != nullptr ? "Drag a knob; double-click it to return to the default. Hover to read the value."
                                            : "Select an effect in the chain to edit its parameters.",
                          juce::dontSendNotification);

    // A plugin has its own window instead of knobs.
    auto* plugin = asPlugin (effect);
    m_openPlugin.setVisible (plugin != nullptr);

    if (plugin != nullptr)
    {
        const auto& description = plugin->getDescription();
        m_openPlugin.setEnabled (! plugin->isMissing() && ! m_session.isEditLocked());
        m_paramsHint.setText (plugin->isMissing()
                                  ? description.name + " (" + formatLabel (description) + ") can't be loaded: " + plugin->getLoadError()
                                        + ". Audio passes through unchanged; its settings are kept and come back once it is installed."
                                  : description.name + " by " + description.manufacturerName + " (" + formatLabel (description)
                                        + "). This slot has its own instance: its settings are saved with the banks.",
                              juce::dontSendNotification);
    }

    if (effect != nullptr)
    {
        const auto effectIndex = m_selectedEffect;
        const auto ref = m_selected;

        for (const auto& parameter : effect->getParameters())
        {
            auto* knob = m_knobs.add (new ui::Knob (parameter.id, parameter.minimum < 0.0f && parameter.maximum > 0.0f
                                                                    && juce::approximatelyEqual (parameter.minimum, -parameter.maximum)));
            knob->setRange (parameter.minimum, parameter.maximum, 0.01);
            knob->setValue (parameter.value, juce::dontSendNotification);
            knob->setDoubleClickReturnValue (true, parameter.defaultValue);
            knob->setTextValueSuffix (parameter.unit.isEmpty() ? juce::String() : " " + parameter.unit);
            knob->setEnabled (! m_session.isEditLocked());

            const auto id = parameter.id;
            knob->onValueChange = [this, knob, id, effectIndex, ref]
            {
                if (knob->isMouseButtonDown())
                    return;   // committed on drag end, so a drag is one edit

                const auto value = (float) knob->getValue();
                m_session.editBanks ([&] (MBoxApplication::Banks& banks)
                                     {
                                         auto& chain = banks[(size_t) ref.bank].m_bankSlots[(size_t) ref.slot].m_effectChain;
                                         return std::next (chain.begin(), effectIndex)->getEffect().setParameter (id, value);
                                     },
                                     id + " = " + juce::String (value, 2));
            };
            knob->onDragEnd = knob->onValueChange;
            m_paramsPanel.addAndMakeVisible (knob);
        }
    }

    resized();
}

void BanksEditor::addEffect (const juce::String& type)
{
    const auto ref = m_selected;
    const auto result = m_session.editBanks ([&] (MBoxApplication::Banks& banks)
                                             {
                                                 banks[(size_t) ref.bank].m_bankSlots[(size_t) ref.slot].m_effectChain.emplace_back (AudioEffectFactory::create (type));
                                                 return juce::Result::ok();
                                             },
                                             type + " added to " + ref.toString());

    if (result.wasOk())
    {
        m_selectedEffect = (int) slot().m_effectChain.size() - 1;
        sessionChanged();
    }
}

void BanksEditor::showPluginMenu()
{
    PluginMenu::show (m_session, m_addPlugin, [this] (const juce::PluginDescription& description) { addPlugin (description); });
}

void BanksEditor::addPlugin (const juce::PluginDescription& description)
{
    if (! m_session.canEdit())
    {
        m_session.setStatus ("Playback is active", true);
        return;
    }

    // A new, independent instance: the same plugin in another slot keeps its own settings.
    auto plugin = PluginMenu::create (m_session, description);

    if (plugin == nullptr)
        return;

    auto* added = plugin.get();
    const auto ref = m_selected;
    const auto result = m_session.editBanks ([&] (MBoxApplication::Banks& banks)
                                             {
                                                 banks[(size_t) ref.bank].m_bankSlots[(size_t) ref.slot].m_effectChain.emplace_back (std::move (plugin));
                                                 return juce::Result::ok();
                                             },
                                             description.name + " added to " + ref.toString());

    if (result.wasOk())
    {
        m_selectedEffect = (int) slot().m_effectChain.size() - 1;
        sessionChanged();
        openPluginWindow (*added);
    }
}

void BanksEditor::openPluginWindow (MAudioEffect& plugin)
{
    // Edits in the plugin's window change the banks: mark them unsaved. (The session outlives the banks' plugins.)
    plugin.onStateChanged = [&session = m_session] { session.markBanksChanged(); };
    plugin.showEditor (plugin.getName() + " - " + UIHelpers::bankSlotName (m_session.getApp().getBanks(), m_selected));
}

void BanksEditor::closePluginWindows()
{
    for (auto& bank : m_session.getApp().getBanks())
        for (auto& bankSlot : bank.m_bankSlots)
            for (auto& instance : bankSlot.m_effectChain)
                if (auto* plugin = asPlugin (&instance.getEffect()))
                    plugin->closeEditor();
}

void BanksEditor::moveEffect (int delta)
{
    const auto from = m_selectedEffect;
    const auto to = from + delta;
    const auto ref = m_selected;

    const auto result = m_session.editBanks ([&] (MBoxApplication::Banks& banks)
                                             {
                                                 auto& chain = banks[(size_t) ref.bank].m_bankSlots[(size_t) ref.slot].m_effectChain;

                                                 if (from < 0 || to < 0 || to >= (int) chain.size())
                                                     return juce::Result::fail ("Cannot move the effect there");

                                                 auto a = std::next (chain.begin(), from), b = std::next (chain.begin(), to);
                                                 std::iter_swap (a, b);
                                                 return juce::Result::ok();
                                             });

    if (result.wasOk())
    {
        m_selectedEffect = to;
        sessionChanged();
    }
}

void BanksEditor::removeEffect()
{
    const auto index = m_selectedEffect;
    const auto ref = m_selected;

    m_session.editBanks ([&] (MBoxApplication::Banks& banks)
                         {
                             auto& chain = banks[(size_t) ref.bank].m_bankSlots[(size_t) ref.slot].m_effectChain;

                             if (index < 0 || index >= (int) chain.size())
                                 return juce::Result::fail ("Select an effect first");

                             chain.erase (std::next (chain.begin(), index));
                             return juce::Result::ok();
                         },
                         "Effect removed");
}

//==============================================================================
LoudnessIntegrator& BanksEditor::integrator (BankSlotRef ref)
{
    return m_integrators[{ ref.bank, ref.slot }];
}

void BanksEditor::refreshInputMenu()
{
    // The device's inputs once it is open; until then a generic list (opening the device here could
    // ask for the microphone just because the window opened).
    const auto* engine = std::as_const (m_session.getApp()).getAudioEngine();
    const auto names = engine != nullptr && engine->isInitialised() ? engine->getInputChannelNames() : juce::StringArray();
    const auto count = names.isEmpty() ? 8 : names.size();

    m_input.clear (juce::dontSendNotification);
    m_input.addItem ("Input: Off", 1);

    for (int channel = 0; channel < count; ++channel)
        m_input.addItem ("Input " + juce::String (channel + 1) + (channel < names.size() ? "   " + names[channel] : juce::String()), channel + 2);

    m_input.setSelectedId (m_monitorInput >= 0 ? m_monitorInput + 2 : 1, juce::dontSendNotification);
}

void BanksEditor::inputChosen()
{
    const auto id = m_input.getSelectedId();

    if (id <= 1)
    {
        stopMonitoring();
        m_session.setStatus ("Bank monitoring off");
        return;
    }

    drainLoudness();
    m_monitorInput = id - 2;
    m_monitorKey.clear();
    startMonitoring();
}

juce::String BanksEditor::monitorKey() const
{
    const auto* engine = std::as_const (m_session.getApp()).getAudioEngine();
    juce::String key;
    key << m_selected.bank << '/' << m_selected.slot << '/' << m_monitorInput << '/'
        << (engine != nullptr ? engine->getDeviceGeneration() : 0) << ':';

    for (const auto& instance : m_session.getApp().getBanks()[(size_t) m_selected.bank].m_bankSlots[(size_t) m_selected.slot].m_effectChain)
        key << (juce::int64) instance.getEffect().getInstanceId() << ' ';

    return key;
}

MBoxTrack::Outputs BanksEditor::monitorOutputs (juce::String* description)
{
    auto text = [] (MBoxTrack::Outputs outputs)
    {
        return outputs.second == -1 ? juce::String (outputs.first) + " mono" : juce::String (outputs.first) + "/" + juce::String (outputs.second);
    };

    if (auto* song = m_session.getSelectedSong())
    {
        const auto& mix = song->getMix (m_session.getSelectedMixIndex());
        auto outputs = mix.getMaster().getOutputs();

        if (outputs == MBoxTrack::Outputs { 0, 0 })
            outputs = { 1, 2 };

        if (outputs != MBoxMaster::noOutputs)
        {
            if (description != nullptr)
                *description = mix.getName() + " (" + text (outputs) + ")";

            return outputs;
        }
    }

    if (description != nullptr)
        *description = "outputs 1/2";

    return { 1, 2 };
}

void BanksEditor::startMonitoring()
{
    if (m_monitorInput < 0)
        return;

    if (m_session.isEditLocked())
    {
        stopMonitoring ("Stop the song to monitor a bankslot");
        return;
    }

    if (auto r = m_session.getApp().initialiseAudio(); r.failed())
    {
        stopMonitoring (r.getErrorMessage());
        return;
    }

    auto* engine = m_session.getApp().getAudioEngine();
    juce::String heardOn;
    const auto outputs = monitorOutputs (&heardOn);

    if (auto r = engine->startBankMonitor (slot(), m_monitorInput, outputs); r.failed())
    {
        stopMonitoring (r.getErrorMessage());
        return;
    }

    // A new signal (another slot or chain): no 400 ms block may span the gap. The slot's history stays.
    integrator (m_selected).clearWindow();
    m_monitorRef = m_selected;
    m_monitorKey = monitorKey();
    refreshInputMenu();   // the device may have just opened: its own input names
    m_session.setStatus ("Monitoring input " + juce::String (m_monitorInput + 1) + " through "
                         + UIHelpers::bankSlotName (m_session.getApp().getBanks(), m_selected) + ", heard on " + heardOn);
    updateLoudnessDisplay();
}

void BanksEditor::stopMonitoring (const juce::String& reason)
{
    drainLoudness();

    if (auto* engine = m_session.getApp().getAudioEngine())
        engine->stopBankMonitor();

    m_monitorInput = -1;
    m_monitorKey.clear();
    m_input.setSelectedId (1, juce::dontSendNotification);
    m_meter.setLevels (std::nullopt, std::nullopt, slot().m_loudness);

    if (reason.isNotEmpty())
        m_session.setStatus (reason, true);
}

void BanksEditor::drainLoudness()
{
    auto* engine = m_session.getApp().getAudioEngine();

    if (engine == nullptr || m_monitorInput < 0 || ! engine->isBankMonitoring())
        return;

    auto& measured = integrator (m_monitorRef);
    auto received = false;
    engine->drainBankLoudness ([&] (double meanSquare) { measured.addBlock (meanSquare); received = true; });

    // Recorded once a 400 ms block has passed the gates (silence never overwrites a value).
    if (received)
        if (const auto integrated = measured.getIntegrated())
            m_session.setSlotLoudness (m_monitorRef, std::round (*integrated * 10.0f) / 10.0f);
}

void BanksEditor::timerCallback()
{
    const auto* engine = std::as_const (m_session.getApp()).getAudioEngine();

    // Follow the device: its own input names once open, or after a change of device.
    if (engine != nullptr && engine->isInitialised() && m_input.getNumItems() != engine->getInputChannelNames().size() + 1)
        refreshInputMenu();

    if (m_monitorInput >= 0)
    {
        if (m_session.isEditLocked())
            stopMonitoring ("Bank monitoring stopped: a song is playing");
        else if (! isShowing() || engine == nullptr || ! engine->isBankMonitoring())
            stopMonitoring();
        else
        {
            drainLoudness();

            if (monitorKey() != m_monitorKey)
                startMonitoring();   // another slot, a changed chain or a restarted device
            else
                m_session.getApp().getAudioEngine()->setBankMonitorOutputs (monitorOutputs());
        }
    }

    updateLoudnessDisplay();
}

void BanksEditor::updateLoudnessDisplay()
{
    const auto recorded = slot().m_loudness;
    const auto live = m_monitorInput >= 0 && m_monitorRef == m_selected;
    auto& measured = integrator (m_selected);

    m_meter.setLevels (live ? measured.getMomentary() : std::nullopt, live ? measured.getShortTerm() : std::nullopt, recorded);

    const auto text = recorded.has_value() ? juce::String (*recorded, 1) + " LUFS" : juce::String ("-- LUFS");

    if (m_loudnessValue.getText() != text)
    {
        m_loudnessValue.setText (text, juce::dontSendNotification);
        m_slotList->repaint();
    }

    m_resetLoudness.setEnabled (recorded.has_value() || measured.getIntegrated().has_value());
}

void BanksEditor::resetLoudness()
{
    integrator (m_selected).reset();
    m_session.setSlotLoudness (m_selected, std::nullopt);
    m_session.setStatus ("Loudness of " + UIHelpers::bankSlotName (m_session.getApp().getBanks(), m_selected) + " reset");
    updateLoudnessDisplay();
    m_slotList->repaint();
}

void BanksEditor::resized()
{
    auto area = getLocalBounds().reduced (16);

    m_slotsPanel.setBounds (area.removeFromLeft (260));
    m_slotViewport.setBounds (m_slotsPanel.getContentBounds());
    m_slotList->setSize (m_slotViewport.getMaximumVisibleWidth(), m_slotList->totalHeight());
    area.removeFromLeft (16);

    m_paramsPanel.setBounds (area.removeFromBottom (230));
    area.removeFromBottom (16);
    m_slotPanel.setBounds (area);

    // Slot panel
    auto slotArea = m_slotPanel.getContentBounds();
    m_title.setBounds (slotArea.removeFromTop (34));
    slotArea.removeFromTop (6);

    auto names = slotArea.removeFromTop (30);
    m_slotNameCaption.setBounds (names.removeFromLeft (80));
    m_slotName.setBounds (names.removeFromLeft (180));
    names.removeFromLeft (20);
    m_bankNameCaption.setBounds (names.removeFromLeft (80));
    m_bankName.setBounds (names.removeFromLeft (180));

    slotArea.removeFromTop (12);
    m_chainHeading.setBounds (slotArea.removeFromTop (22));
    slotArea.removeFromTop (4);

    // The loudness column on the right: value, meter and input menu over the chain's height, Reset
    // in the button row below them.
    constexpr int loudnessWidth = 104;
    auto buttons = slotArea.removeFromBottom (34);
    m_resetLoudness.setBounds (buttons.removeFromRight (loudnessWidth));
    buttons.removeFromRight (10);

    for (auto* button : m_addButtons)
    {
        button->setBounds (buttons.removeFromLeft (100));
        buttons.removeFromLeft (6);
    }

    m_addPlugin.setBounds (buttons.removeFromLeft (100));

    m_remove.setBounds (buttons.removeFromRight (84));
    buttons.removeFromRight (6);
    m_down.setBounds (buttons.removeFromRight (62));
    buttons.removeFromRight (6);
    m_up.setBounds (buttons.removeFromRight (52));

    slotArea.removeFromBottom (8);
    auto loudness = slotArea.removeFromRight (loudnessWidth);
    slotArea.removeFromRight (10);
    m_chainList->setBounds (slotArea);

    m_loudnessValue.setBounds (loudness.removeFromTop (28));
    loudness.removeFromTop (4);
    m_input.setBounds (loudness.removeFromBottom (26));
    loudness.removeFromBottom (4);
    m_meter.setBounds (loudness);

    // Parameters: knobs in a row
    auto params = m_paramsPanel.getContentBounds();
    m_paramsHint.setBounds (params.removeFromBottom (20));
    m_openPlugin.setBounds (params.withSizeKeepingCentre (200, 36));

    if (! m_knobs.isEmpty())
    {
        const auto width = juce::jmin (130, params.getWidth() / m_knobs.size());

        for (auto* knob : m_knobs)
            knob->setBounds (params.removeFromLeft (width).reduced (4));
    }
}
