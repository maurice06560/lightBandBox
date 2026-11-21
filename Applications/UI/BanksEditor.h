#pragma once

#include "PluginWindows.h"
#include "Session.h"
#include "UIHelpers.h"

/** Edits the banks (5 banks x 5 bankslots, rack-wide): their names, and each slot's effect chain
    of AU / VST3 plugins (order matters, signal top to bottom). Shown in its own window.

    A plugin opens in its own window: the running instance when the loaded playlist uses the
    slot (heard at once), an instance of its own otherwise; its settings are saved with the banks.
    The chains and names don't change while a song plays or is paused; plugin windows still work.
*/
class BanksEditor final : public juce::Component,
                          private Session::Listener
{
public:
    BanksEditor (Session& session, PluginWindows& pluginWindows);
    ~BanksEditor() override;

    void selectSlot (MBoxSlotRef ref);

    void resized() override;

private:
    class SlotList;
    class ChainList;

    void sessionChanged() override;
    void sessionMixChanged() override {}

    MBoxBankSlot& slot();
    const MBoxEffect* selectedEffect();
    juce::String effectTitle (int index);

    void showPluginMenu();
    void addPlugin (const juce::PluginDescription& description);
    void openSelected();
    void moveEffect (int delta);
    void removeEffect();
    void toggleBypass();

    Session& m_session;
    PluginWindows& m_pluginWindows;
    MBoxSlotRef m_selected;
    int m_selectedEffect = -1;

    std::unique_ptr<SlotList> m_slotList;
    juce::Viewport m_slotViewport;
    std::unique_ptr<ChainList> m_chainList;
    ui::Panel m_slotsPanel { "Banks" };
    ui::Panel m_slotPanel { "Bankslot" };
    ui::Panel m_pluginPanel { "Plugin" };

    ui::Label m_title { {}, ui::Label::Style::Title };
    ui::Label m_slotNameCaption { "Slot name", ui::Label::Style::Caption };
    ui::Label m_slotName { {}, ui::Label::Style::Value };
    ui::Label m_bankNameCaption { "Bank name", ui::Label::Style::Caption };
    ui::Label m_bankName { {}, ui::Label::Style::Value };
    ui::Label m_chainHeading { "Effect chain (signal flows top to bottom)", ui::Label::Style::Heading };

    ui::Button m_addPlugin { "+ Plugin...", ui::Button::Variant::Accent };
    ui::Button m_up { "Up" };
    ui::Button m_down { "Down" };
    ui::Button m_remove { "Remove", ui::Button::Variant::Warm };

    ui::Label m_pluginInfo { {}, ui::Label::Style::Body };
    ui::Button m_openPlugin { "Open plugin window", ui::Button::Variant::Accent };
    ui::Button m_bypass { "Bypass", ui::Button::Variant::Standard, true };
    ui::Label m_pluginHint { {}, ui::Label::Style::Caption };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (BanksEditor)
};
