#pragma once

#include "../Audio/LoudnessMeter.h"
#include "../Effects/MAudioEffect.h"
#include "LoudnessMeterView.h"
#include "Session.h"
#include "UIHelpers.h"

#include <map>

/** Edits the 5 banks x 4 bankslots: names, each slot's effect chain (order matters), and the
    parameters of the selected effect as knobs. Shown in its own window.

    Besides the built-in effects, any installed AU / VST3 effect plugin can be added (MAudioEffect).
    Each one is its own instance, edited in the plugin's own window; those windows close while a
    song plays or is paused, since banks can't change then.

    Loudness: the Input menu plays a device input through the selected slot (the bank's own effects,
    so edits are heard at once) to the outputs of the mix selected in the main window, and meters it
    (LUFS, BS.1770). Each slot's integrated loudness is recorded next to its name and saved with the
    banks; Reset clears it. Monitoring only runs while the song is stopped and the window is shown.
*/
class BanksEditor final : public juce::Component,
                          private Session::Listener,
                          private juce::Timer
{
public:
    explicit BanksEditor (Session& session);
    ~BanksEditor() override;

    void selectSlot (BankSlotRef ref);

    void resized() override;

private:
    class SlotList;
    class ChainList;

    void sessionChanged() override;

    void sessionMixChanged() override {}
    void rebuildKnobs();
    /** Updates the knobs in place when they still belong to the selected effect; rebuilds them otherwise.
        (Rebuilding from inside a knob's own callback would delete it mid-call.)
    */
    void refreshKnobs();
    MBoxBankSlot& slot();
    IAudioEffect* selectedEffect();

    void addEffect (const juce::String& type);
    void showPluginMenu();
    void addPlugin (const juce::PluginDescription& description);
    void openPluginWindow (MAudioEffect& plugin);
    void closePluginWindows();
    void moveEffect (int delta);
    void removeEffect();

    // Loudness monitoring
    void timerCallback() override;
    void refreshInputMenu();
    void inputChosen();
    void stopMonitoring (const juce::String& reason = {});
    /** Starts (or restarts, after a change of slot, chain or device) the engine's bank monitor. */
    void startMonitoring();
    /** What the running monitor was started with: a change means it must start again. */
    juce::String monitorKey() const;
    /** The selected mix's outputs (1/2 when it has none, or without a song), and a text for them. */
    MBoxTrack::Outputs monitorOutputs (juce::String* description = nullptr);
    /** Feeds the monitor's new 100 ms blocks to the measured slot and records its integrated loudness. */
    void drainLoudness();
    void resetLoudness();
    void updateLoudnessDisplay();
    LoudnessIntegrator& integrator (BankSlotRef ref);

    Session& m_session;
    BankSlotRef m_selected;
    int m_selectedEffect = -1;

    std::unique_ptr<SlotList> m_slotList;
    juce::Viewport m_slotViewport;
    std::unique_ptr<ChainList> m_chainList;
    ui::Panel m_slotsPanel { "Banks" };
    ui::Panel m_slotPanel { "Bankslot" };
    ui::Panel m_paramsPanel { "Parameters" };

    ui::Label m_title { {}, ui::Label::Style::Title };
    ui::Label m_slotNameCaption { "Slot name", ui::Label::Style::Caption };
    ui::Label m_slotName { {}, ui::Label::Style::Value };
    ui::Label m_bankNameCaption { "Bank name", ui::Label::Style::Caption };
    ui::Label m_bankName { {}, ui::Label::Style::Value };
    ui::Label m_chainHeading { "Effect chain (signal flows top to bottom)", ui::Label::Style::Heading };

    juce::OwnedArray<ui::Button> m_addButtons;
    ui::Button m_up { "Up" };
    ui::Button m_down { "Down" };
    ui::Button m_remove { "Remove", ui::Button::Variant::Warm };
    ui::Button m_addPlugin { "+ Plugin...", ui::Button::Variant::Accent };
    ui::Button m_openPlugin { "Open plugin window", ui::Button::Variant::Accent };

    LoudnessMeterView m_meter;
    ui::Label m_loudnessValue { {}, ui::Label::Style::Value };
    juce::ComboBox m_input;
    ui::Button m_resetLoudness { "Reset" };
    int m_monitorInput = -1;                 // 0-based device input while monitoring, -1 when off
    BankSlotRef m_monitorRef;                // the slot the running monitor measures
    juce::String m_monitorKey;
    std::map<std::pair<int, int>, LoudnessIntegrator> m_integrators;   // per slot, for this session

    juce::OwnedArray<ui::Knob> m_knobs;
    juce::String m_knobsFor;   // "bank/slot/effect index/effect name" the knobs were built for
    ui::Label m_paramsHint { {}, ui::Label::Style::Caption };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (BanksEditor)
};
