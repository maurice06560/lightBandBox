#pragma once

#include <onebox_ui/onebox_ui.h>

namespace ui = onebox::ui;

/** The small window shown while the app starts (loading the data folder, opening the audio device),
    instead of the system's busy cursor. It is its own desktop window, without a title bar, centred
    on the main display.

    It only redraws when the message loop runs: whoever drives the startup does it in steps
    (see OneBoxBandApplication::initialise), calling setStep() between them.
*/
class StartupSplash final : public juce::Component
{
public:
    explicit StartupSplash (const juce::String& version);
    ~StartupSplash() override;

    /** Opens it as its own window, centred on the main display. */
    void show();

    /** What is being done now, and how far the startup is (0 to 1). */
    void setStep (const juce::String& message, double progress);

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    ui::Label m_title { "OneBoxBand", ui::Label::Style::Title };
    ui::Label m_version;
    ui::Label m_message { "Starting...", ui::Label::Style::Body };
    juce::Rectangle<float> m_bar;
    double m_progress = 0.0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (StartupSplash)
};
