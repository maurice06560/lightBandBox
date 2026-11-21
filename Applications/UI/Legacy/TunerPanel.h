#pragma once

#include "Session.h"
#include "UIHelpers.h"
#include "../Audio/PitchDetector.h"

#include <deque>

/** The Tuner window's content: an input menu and a chromatic tuner (note, cents, frequency).

    While it runs (start() to stop(), i.e. while the window is open) the chosen input goes to the
    tuner only: MBoxAudioEngine::startTuner() silences it for the tracks, their monitoring and DI
    sends and the bank monitor, on every output. stop() gives it back to all of them.
*/
class TunerPanel final : public juce::Component,
                         private juce::Timer
{
public:
    explicit TunerPanel (Session& session);
    ~TunerPanel() override;

    /** Opens the audio device if needed and takes the chosen input. */
    void start();
    void stop();

    /** Shows a reading as if detected (for --snapshot); nullopt for none. */
    void showReading (std::optional<double> frequency);

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;
    void refreshInputMenu();
    void inputChosen();
    void applyInput();
    void updateFooter();

    Session& m_session;
    bool m_running = false;
    int m_input = -1;              // 0-based; -1 until first started (then the selected track's input)
    int m_menuGeneration = -1;     // the device generation the menu was built for

    PitchDetector m_detector;
    std::deque<double> m_recent;   // the last few detections, for a median
    std::optional<double> m_frequency;
    double m_shownCents = 0.0;
    double m_lastDetection = 0.0;  // Time::getMillisecondCounterHiRes() of the last pitch

    ui::Label m_inputCaption { "Input", ui::Label::Style::Caption };
    juce::ComboBox m_inputMenu;
    ui::Label m_footer { {}, ui::Label::Style::Caption };
    juce::Rectangle<int> m_display;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TunerPanel)
};
