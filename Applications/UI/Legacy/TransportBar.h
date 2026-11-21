#pragma once

#include "Session.h"
#include "UIHelpers.h"

/** Top bar: transport buttons, position readouts (bar, time, tempo and signature at the playhead),
    metronome toggle, the tuner, and the app actions (Save, Banks, Audio device, Load folder).
*/
class TransportBar final : public juce::Component,
                           private Session::Listener,
                           private juce::Timer
{
public:
    explicit TransportBar (Session& session);
    ~TransportBar() override;

    std::function<void()> onShowBanks, onShowAudioDevice, onLoadFolder, onToggleTuner;
    std::function<void()> onToggleBrowser, onToggleEditor;

    /** Lights the panel toggles to match what is shown. */
    void setPanelsShown (bool browser, bool editor);
    /** Lights the tuner button while the Tuner window is open. */
    void setTunerShown (bool shown);

    void togglePlayPause();
    void stop();

    void resized() override;
    void paint (juce::Graphics&) override;

private:
    void sessionChanged() override;
    void timerCallback() override;
    void updateReadouts();

    Session& m_session;

    ui::IconButton m_browserToggle { ui::IconButton::Icon::SidebarLeft, ui::Button::Variant::Standard, true };
    ui::IconButton m_editorToggle { ui::IconButton::Icon::SidebarRight, ui::Button::Variant::Standard, true };
    ui::IconButton m_play { ui::IconButton::Icon::Play, ui::Button::Variant::Accent };
    ui::IconButton m_stop { ui::IconButton::Icon::Stop };
    ui::IconButton m_metronome { ui::IconButton::Icon::Metronome, ui::Button::Variant::Standard, true };
    ui::IconButton m_tuner { ui::IconButton::Icon::Tuner, ui::Button::Variant::Standard, true };
    ui::Button m_test { "Test" };   // TEMPORARY (PluginTest), left of the tuner

    ui::Label m_songTitle { {}, ui::Label::Style::Title };
    ui::Label m_songCaption { {}, ui::Label::Style::Caption };
    /** The position readout. Bar, beat, division and tick each get a fixed slot sized for their widest
        value, so a field gaining a digit (a tick going 9 -> 10 -> 100) never shifts the others.
        Other text (the count-in) is shown whole, like a plain label.
    */
    class PositionReadout final : public ui::Label
    {
    public:
        PositionReadout() : ui::Label ({}, ui::Label::Style::Value) {}

        void setPosition (const Bar& bar);
        void setMessage (const juce::String& message);
        void paint (juce::Graphics&) override;

    private:
        std::optional<Bar> m_bar;
    };

    PositionReadout m_position;
    ui::Label m_time { "00:00.0", ui::Label::Style::Value };
    ui::Label m_tempo { "120 BPM", ui::Label::Style::Value };
    ui::Label m_signature { "4/4", ui::Label::Style::Value };
    ui::Label m_positionCaption { "Position", ui::Label::Style::Caption };
    ui::Label m_timeCaption { "Time", ui::Label::Style::Caption };
    ui::Label m_tempoCaption { "Tempo", ui::Label::Style::Caption };
    ui::Label m_signatureCaption { "Signature", ui::Label::Style::Caption };

    ui::Button m_save { "Save" };
    ui::Button m_banks { "Banks" };
    ui::Button m_audio { "Audio" };
    ui::Button m_load { "Load" };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TransportBar)
};
