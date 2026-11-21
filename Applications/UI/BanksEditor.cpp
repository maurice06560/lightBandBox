#include "BanksEditor.h"
#include "PluginMenu.h"

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
        const auto banks = m_owner.m_session.getBanks();
        int y = 2;

        for (int b = 1; b <= numberOfBanks; ++b)
        {
            const auto& bank = banks[(size_t) b - 1];
            const auto colour = UIHelpers::bankColour (b);
            auto header = juce::Rectangle<float> (2.0f, (float) y, (float) getWidth() - 4.0f, (float) bankRowHeight - 2.0f);
            g.setColour (colour.withAlpha (0.18f));
            g.fillRoundedRectangle (header, 4.0f);
            g.setColour (colour);
            g.fillRoundedRectangle (header.withWidth (4.0f), 2.0f);
            g.setColour (palette.text.brighter (0.3f));
            g.setFont (ui::LookAndFeel::makeFont (13.0f, true));
            g.drawText (bank.m_name, header.reduced (12.0f, 0.0f), juce::Justification::centredLeft, true);
            y += bankRowHeight;

            for (int s = 1; s <= numberOfSlotsPerBanks; ++s)
            {
                const auto& slot = bank.m_bankSlots[(size_t) s - 1];
                auto row = juce::Rectangle<float> (2.0f, (float) y, (float) getWidth() - 4.0f, (float) slotRowHeight - 2.0f);
                const auto selected = m_owner.m_selected == MBoxSlotRef { b, s };

                if (selected)
                {
                    g.setColour (palette.accent.withAlpha (0.85f));
                    g.fillRoundedRectangle (row, 4.0f);
                }

                g.setColour (selected ? palette.textOnAccent : palette.text);
                g.setFont (ui::LookAndFeel::makeFont (12.5f));
                g.drawText (slot.m_name, row.reduced (22.0f, 0.0f).withTrimmedRight (30.0f), juce::Justification::centredLeft, true);

                g.setColour (selected ? palette.textOnAccent.withAlpha (0.7f) : palette.textDim.brighter (0.2f));
                g.setFont (ui::LookAndFeel::makeFont (11.0f));
                g.drawText (slot.m_effectChain.empty() ? juce::String ("dry") : juce::String ((int) slot.m_effectChain.size()) + " fx",
                            row.reduced (10.0f, 0.0f), juce::Justification::centredRight, false);
                y += slotRowHeight;
            }
        }
    }

    int totalHeight() const { return 4 + numberOfBanks * (bankRowHeight + numberOfSlotsPerBanks * slotRowHeight); }

    void mouseDown (const juce::MouseEvent& e) override
    {
        int y = 2;

        for (int b = 1; b <= numberOfBanks; ++b)
        {
            y += bankRowHeight;

            for (int s = 1; s <= numberOfSlotsPerBanks; ++s)
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

//==============================================================================
/** The selected slot's effects, in processing order. Double-click one to open its window. */
class BanksEditor::ChainList final : public juce::Component
{
public:
    explicit ChainList (BanksEditor& owner) : m_owner (owner) {}

    static constexpr int rowHeight = 34;

    void paint (juce::Graphics& g) override
    {
        const auto& palette = ui::getTheme (*this).getPalette();
        const auto& chain = m_owner.slot().m_effectChain;

        g.setColour (palette.arcTrack.withAlpha (0.7f));
        g.fillRoundedRectangle (getLocalBounds().toFloat(), 6.0f);

        if (chain.empty())
        {
            g.setColour (palette.textDim.brighter (0.2f));
            g.setFont (ui::LookAndFeel::makeFont (12.5f));
            g.drawText ("Dry: no effects. Add a plugin with the button below.", getLocalBounds(), juce::Justification::centred, false);
            return;
        }

        for (int index = 0; index < (int) chain.size(); ++index)
        {
            const auto& effect = chain[(size_t) index];
            auto row = juce::Rectangle<float> (4.0f, 4.0f + (float) (index * rowHeight), (float) getWidth() - 8.0f, (float) rowHeight - 4.0f);
            const auto selected = index == m_owner.m_selectedEffect;

            g.setGradientFill (juce::ColourGradient (selected ? palette.accent : palette.capTop, 0.0f, row.getY(),
                                                     selected ? palette.accent.darker (0.3f) : palette.capBottom, 0.0f, row.getBottom(), false));
            g.fillRoundedRectangle (row, 5.0f);

            g.setColour ((selected ? palette.textOnAccent : palette.text.brighter (0.3f)).withMultipliedAlpha (effect.m_bypassed ? 0.5f : 1.0f));
            g.setFont (ui::LookAndFeel::makeFont (13.0f, true));
            g.drawText (juce::String (index + 1) + ".  " + effect.m_name + "  (" + PluginMenu::formatLabel (effect.m_format) + ")"
                            + (effect.m_bypassed ? "   bypassed" : ""),
                        row.reduced (12.0f, 0.0f), juce::Justification::centredLeft, true);
        }
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        const auto index = (e.y - 4) / rowHeight;

        if (index >= 0 && index < (int) m_owner.slot().m_effectChain.size())
        {
            m_owner.m_selectedEffect = index;
            m_owner.sessionChanged();
        }
    }

    void mouseDoubleClick (const juce::MouseEvent& e) override
    {
        if ((e.y - 4) / rowHeight == m_owner.m_selectedEffect)
            m_owner.openSelected();
    }

private:
    BanksEditor& m_owner;
};

//==============================================================================
BanksEditor::BanksEditor (Session& session, PluginWindows& pluginWindows)
    : m_session (session), m_pluginWindows (pluginWindows)
{
    m_slotList = std::make_unique<SlotList> (*this);
    m_chainList = std::make_unique<ChainList> (*this);

    for (auto* c : std::initializer_list<juce::Component*> { &m_slotsPanel, &m_slotPanel, &m_pluginPanel })
        addAndMakeVisible (c);

    m_slotViewport.setViewedComponent (m_slotList.get(), false);
    m_slotViewport.setScrollBarsShown (true, false);
    m_slotViewport.setScrollBarThickness (8);
    m_slotsPanel.addAndMakeVisible (m_slotViewport);

    for (auto* c : std::initializer_list<juce::Component*> { &m_title, &m_slotNameCaption, &m_slotName, &m_bankNameCaption, &m_bankName,
                                                              &m_chainHeading, m_chainList.get(), &m_addPlugin, &m_up, &m_down, &m_remove })
        m_slotPanel.addAndMakeVisible (c);

    for (auto* c : std::initializer_list<juce::Component*> { &m_pluginInfo, &m_openPlugin, &m_bypass, &m_pluginHint })
        m_pluginPanel.addAndMakeVisible (c);

    m_pluginInfo.setJustificationType (juce::Justification::centredLeft);
    m_bypass.setTooltip ("Bypassed: the plugin passes audio through unchanged (its latency is kept)");

    m_addPlugin.onClick = [this] { showPluginMenu(); };
    m_openPlugin.onClick = [this] { openSelected(); };
    m_bypass.onClick = [this] { toggleBypass(); };
    m_up.onClick = [this] { moveEffect (-1); };
    m_down.onClick = [this] { moveEffect (1); };
    m_remove.onClick = [this] { removeEffect(); };

    m_slotName.setEditable (false, true);
    m_slotName.onTextChange = [this]
    {
        const auto name = m_slotName.getText().trim();
        const auto ref = m_selected;
        m_session.editBanks ([&] (std::span<MBoxBank> banks)
                             {
                                 if (name.isEmpty())
                                     return juce::Result::fail ("A slot needs a name");

                                 banks[(size_t) ref.m_bank - 1].m_bankSlots[(size_t) ref.m_slot - 1].m_name = name;
                                 return juce::Result::ok();
                             },
                             "Slot renamed to " + name);
    };

    m_bankName.setEditable (false, true);
    m_bankName.onTextChange = [this]
    {
        const auto name = m_bankName.getText().trim();
        const auto bank = m_selected.m_bank;
        m_session.editBanks ([&] (std::span<MBoxBank> banks)
                             {
                                 if (name.isEmpty())
                                     return juce::Result::fail ("A bank needs a name");

                                 banks[(size_t) bank - 1].m_name = name;
                                 return juce::Result::ok();
                             },
                             "Bank renamed to " + name);
    };

    m_session.addListener (this);
    selectSlot ({ 1, 1 });
    setSize (960, 640);
}

BanksEditor::~BanksEditor()
{
    m_session.removeListener (this);
}

MBoxBankSlot& BanksEditor::slot()
{
    return m_session.getBanks()[(size_t) m_selected.m_bank - 1].m_bankSlots[(size_t) m_selected.m_slot - 1];
}

const MBoxEffect* BanksEditor::selectedEffect()
{
    const auto& chain = slot().m_effectChain;
    return juce::isPositiveAndBelow (m_selectedEffect, (int) chain.size()) ? &chain[(size_t) m_selectedEffect] : nullptr;
}

juce::String BanksEditor::effectTitle (int index)
{
    const auto& chain = slot().m_effectChain;
    const auto name = juce::isPositiveAndBelow (index, (int) chain.size()) ? chain[(size_t) index].m_name : juce::String();
    return name + " - " + UIHelpers::bankSlotName (m_session.getBanks(), m_selected);
}

void BanksEditor::selectSlot (MBoxSlotRef ref)
{
    m_selected = ref;
    m_selectedEffect = slot().m_effectChain.empty() ? -1 : 0;
    sessionChanged();
}

void BanksEditor::sessionChanged()
{
    auto& current = slot();
    m_title.setText (UIHelpers::bankSlotName (m_session.getBanks(), m_selected), juce::dontSendNotification);
    m_slotName.setText (current.m_name, juce::dontSendNotification);
    m_bankName.setText (m_session.getBanks()[(size_t) m_selected.m_bank - 1].m_name, juce::dontSendNotification);

    m_selectedEffect = juce::jmin (m_selectedEffect, (int) current.m_effectChain.size() - 1);

    if (m_selectedEffect < 0 && ! current.m_effectChain.empty())
        m_selectedEffect = 0;

    // Banks can't change while a song plays or is paused: browse only (plugin windows still work)
    const auto unlocked = ! m_session.isEditLocked();
    const auto* effect = selectedEffect();
    m_up.setEnabled (unlocked && effect != nullptr && m_selectedEffect > 0);
    m_down.setEnabled (unlocked && effect != nullptr && m_selectedEffect < (int) current.m_effectChain.size() - 1);
    m_remove.setEnabled (unlocked && effect != nullptr);
    m_slotName.setEnabled (unlocked);
    m_bankName.setEnabled (unlocked);
    m_addPlugin.setEnabled (unlocked);

    m_openPlugin.setEnabled (effect != nullptr);
    m_bypass.setEnabled (effect != nullptr);
    m_bypass.setToggleState (effect != nullptr && effect->m_bypassed, juce::dontSendNotification);
    m_pluginPanel.setTitle (effect != nullptr ? effect->m_name : juce::String ("Plugin"));
    m_pluginInfo.setText (effect != nullptr ? effect->m_name + " by " + effect->m_manufacturer + " (" + PluginMenu::formatLabel (effect->m_format)
                                                  + (effect->m_version.isNotEmpty() ? ", version " + effect->m_version : juce::String()) + ")"
                                            : juce::String ("Select an effect in the chain."),
                          juce::dontSendNotification);
    m_pluginHint.setText (effect != nullptr ? "Its settings are edited in its own window (double-click it in the chain) and saved with the banks."
                                            : juce::String(),
                          juce::dontSendNotification);

    m_slotList->repaint();
    m_chainList->repaint();
}

//==============================================================================
void BanksEditor::showPluginMenu()
{
    PluginMenu::show (m_session, m_addPlugin, [this] (const juce::PluginDescription& description) { addPlugin (description); });
}

void BanksEditor::addPlugin (const juce::PluginDescription& description)
{
    const auto ref = m_selected;
    int index = -1;
    const auto result = m_session.editBanks ([&] (std::span<MBoxBank> banks)
                                             {
                                                 auto& chain = banks[(size_t) ref.m_bank - 1].m_bankSlots[(size_t) ref.m_slot - 1].m_effectChain;
                                                 chain.push_back (MBoxEffect::fromDescription (description));
                                                 index = (int) chain.size() - 1;
                                                 return juce::Result::ok();
                                             },
                                             description.name + " added to " + UIHelpers::bankSlotName (m_session.getBanks(), ref));

    if (result.wasOk())
    {
        m_selectedEffect = index;
        sessionChanged();
        m_pluginWindows.openWhenRunning (PluginWindows::slotTarget (ref), index, effectTitle (index));
    }
}

void BanksEditor::openSelected()
{
    if (selectedEffect() != nullptr)
        m_pluginWindows.open (PluginWindows::slotTarget (m_selected), m_selectedEffect, effectTitle (m_selectedEffect));
}

void BanksEditor::toggleBypass()
{
    const auto* effect = selectedEffect();

    if (effect == nullptr)
        return;

    const auto bypassed = ! effect->m_bypassed;
    const auto result = m_session.getCore().setEffectBypass (PluginWindows::slotTarget (m_selected), m_selectedEffect, bypassed);
    m_session.report (result.wasOk() ? juce::Result::ok() : juce::Result::fail (juce::String (result.m_message)),
                      (bypassed ? "Bypassed " : "Back on: ") + effectTitle (m_selectedEffect));
    m_session.changed();
}

void BanksEditor::moveEffect (int delta)
{
    const auto from = m_selectedEffect;
    const auto to = from + delta;
    const auto ref = m_selected;

    // The windows follow effects by position: close this slot's
    m_pluginWindows.close (PluginWindows::slotTarget (ref));

    const auto result = m_session.editBanks ([&] (std::span<MBoxBank> banks)
                                             {
                                                 auto& chain = banks[(size_t) ref.m_bank - 1].m_bankSlots[(size_t) ref.m_slot - 1].m_effectChain;

                                                 if (from < 0 || to < 0 || to >= (int) chain.size())
                                                     return juce::Result::fail ("Cannot move the effect there");

                                                 std::swap (chain[(size_t) from], chain[(size_t) to]);
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
    m_pluginWindows.close (PluginWindows::slotTarget (ref));

    m_session.editBanks ([&] (std::span<MBoxBank> banks)
                         {
                             auto& chain = banks[(size_t) ref.m_bank - 1].m_bankSlots[(size_t) ref.m_slot - 1].m_effectChain;

                             if (index < 0 || index >= (int) chain.size())
                                 return juce::Result::fail ("Select an effect first");

                             chain.erase (chain.begin() + index);
                             return juce::Result::ok();
                         },
                         "Effect removed");
}

//==============================================================================
void BanksEditor::resized()
{
    auto area = getLocalBounds().reduced (16);

    m_slotsPanel.setBounds (area.removeFromLeft (260));
    m_slotViewport.setBounds (m_slotsPanel.getContentBounds());
    m_slotList->setSize (m_slotViewport.getMaximumVisibleWidth(), m_slotList->totalHeight());
    area.removeFromLeft (16);

    m_pluginPanel.setBounds (area.removeFromBottom (170));
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

    auto buttons = slotArea.removeFromBottom (34);
    m_addPlugin.setBounds (buttons.removeFromLeft (120));
    m_remove.setBounds (buttons.removeFromRight (84));
    buttons.removeFromRight (6);
    m_down.setBounds (buttons.removeFromRight (62));
    buttons.removeFromRight (6);
    m_up.setBounds (buttons.removeFromRight (52));

    slotArea.removeFromBottom (8);
    m_chainList->setBounds (slotArea);

    // Plugin panel
    auto plugin = m_pluginPanel.getContentBounds();
    m_pluginInfo.setBounds (plugin.removeFromTop (28));
    plugin.removeFromTop (10);
    auto row = plugin.removeFromTop (36);
    m_openPlugin.setBounds (row.removeFromLeft (200));
    row.removeFromLeft (10);
    m_bypass.setBounds (row.removeFromLeft (100));
    plugin.removeFromTop (8);
    m_pluginHint.setBounds (plugin.removeFromTop (20));
}
