#pragma once

namespace onebox::ui
{

/** Channel-strip volume fader (Logic-style):

        [ gain readout ][ peak readout ]
        ruler | fader cap  | dB scale | level meter

    - Gain range -inf .. +6 dB, non-linear travel with unity (0 dB) at ~80%.
    - Double-click the fader to return to 0 dB; double-click the gain readout to type a value
      ("-inf" works); click the peak readout to reset the peak hold.
    - All drawing goes through ChannelFader::LookAndFeelMethods, so it follows the active theme.
    - getVolume() / setVolume() speak the same units as MBoxTrack: dB with -infinity for silence.
*/
class ChannelFader : public juce::Component,
                     private juce::Slider::Listener
{
public:
    static constexpr double minDecibels      = -96.0;   // the bottom of the travel, treated as -inf
    static constexpr double maxDecibels      =   6.0;
    static constexpr float  meterMinDecibels = -60.0f;

    /** Narrower than this, the strip uses a compact layout (like Logic's mixer strips). */
    static constexpr int    compactWidth = 100;

    ChannelFader();
    ~ChannelFader() override;

    /** The underlying vertical fader; its value is the gain in dB (minDecibels at the bottom). */
    juce::Slider& getFader() noexcept { return fader; }

    void   setGainDecibels (double dB, juce::NotificationType = juce::sendNotificationAsync);
    double getGainDecibels() const { return fader.getValue(); }

    /** Volume in dB as the backend stores it: -infinity at the bottom of the travel. */
    float getVolume() const;
    /** Accepts -infinity (or anything at or below minDecibels) for silence. */
    void  setVolume (float decibels, juce::NotificationType = juce::sendNotificationAsync);

    /** Called with the new volume (dB, -infinity for silence) whenever the user moves the fader. */
    std::function<void (float)> onVolumeChange;

    /** Purely visual: sets the meter level and peak-hold value, in dB. */
    void setMeterLevel (float levelDb, float peakDb);
    void resetPeak();

    float getMeterLevel() const noexcept { return meterLevelDb; }
    float getMeterPeak()  const noexcept { return meterPeakDb; }

    /** Y position (in this component's coordinates) of the fader cap centre for a gain. */
    float getFaderY (double dB) const;

    /** 0 at the top of the meter (0 dB) .. 1 at the bottom (meterMinDecibels). */
    static float getMeterProportion (float dB);

    static juce::String decibelsToText (double dB);

    //==============================================================================
    struct LookAndFeelMethods
    {
        virtual ~LookAndFeelMethods() = default;

        virtual void drawChannelFaderBackground (juce::Graphics&, juce::Rectangle<float> area, ChannelFader&) = 0;
        virtual void drawChannelFaderReadout    (juce::Graphics&, juce::Rectangle<float> area, bool highlighted, ChannelFader&) = 0;
        virtual void drawChannelFaderRuler      (juce::Graphics&, juce::Rectangle<float> area, ChannelFader&) = 0;
        virtual void drawChannelFaderMeter      (juce::Graphics&, juce::Rectangle<float> scaleArea,
                                                 juce::Rectangle<float> meterArea, ChannelFader&) = 0;
    };

    //==============================================================================
    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    void lookAndFeelChanged() override;

private:
    void sliderValueChanged (juce::Slider*) override;
    void updateReadouts();

    juce::Slider fader;
    juce::Label  gainReadout, peakReadout;

    juce::Rectangle<float> rulerArea, scaleArea, meterArea;

    float meterLevelDb = (float) minDecibels;
    float meterPeakDb  = (float) minDecibels;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ChannelFader)
};

} // namespace onebox::ui
