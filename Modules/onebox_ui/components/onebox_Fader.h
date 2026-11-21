#pragma once

namespace onebox::ui
{

/** A plain console fader (vertical) or slider (horizontal) for any parameter range.
    For track volume with a meter and dB ruler, use ChannelFader.
*/
class Fader : public juce::Slider
{
public:
    enum class Orientation
    {
        Vertical,
        Horizontal
    };

    /** The range defaults to 0..1. */
    explicit Fader (Orientation orientation = Orientation::Vertical);

    /** The value line / fill colour; defaults to the theme accent. */
    void setAccentColour (juce::Colour colour);

    /** Makes a drag move the value by exactly the pointer's travel, so the cap stays under
        the pointer from one end to the other. JUCE otherwise scales a drag that doesn't snap
        to the click to a fixed 250 px. Call it whenever the slider's size changes.
    */
    static void matchDragToTravel (juce::Slider& slider);

    void resized() override;

private:
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Fader)
};

} // namespace onebox::ui
