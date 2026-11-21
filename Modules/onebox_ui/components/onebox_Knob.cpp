namespace onebox::ui
{

Knob::Knob (const juce::String& name, bool bipolar)
{
    LookAndFeel::styleKnob (*this, name, bipolar);
    setRange (bipolar ? -1.0 : 0.0, 1.0);   // a normalised default; call setRange() for real units
}

void Knob::setAccentColour (juce::Colour colour)
{
    setColour (juce::Slider::rotarySliderFillColourId, colour);
}

} // namespace onebox::ui
