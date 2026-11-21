#pragma once

namespace onebox::ui
{

/** A rotary knob: 270-degree sweep, glowing value arc, its name in the cap (the live value while
    hovered or dragged). Bipolar knobs grow their arc from 12 o'clock, for pan-style parameters.
*/
class Knob : public juce::Slider
{
public:
    /** The range defaults to 0..1 (-1..1 when bipolar). */
    explicit Knob (const juce::String& name = {}, bool bipolar = false);

    /** The arc and indicator colour; defaults to the theme accent. */
    void setAccentColour (juce::Colour colour);

private:
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Knob)
};

} // namespace onebox::ui
