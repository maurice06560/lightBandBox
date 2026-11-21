namespace onebox::ui
{

Fader::Fader (Orientation orientation)
{
    setSliderStyle (orientation == Orientation::Vertical ? juce::Slider::LinearVertical
                                                         : juce::Slider::LinearHorizontal);
    setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
    setRange (0.0, 1.0);   // a normalised default; call setRange() for real units
    setSliderSnapsToMousePosition (false);   // grab the cap, don't jump to the click
}

void Fader::setAccentColour (juce::Colour colour)
{
    setColour (juce::Slider::rotarySliderFillColourId, colour);
}

void Fader::matchDragToTravel (juce::Slider& slider)
{
    // The same travel the slider maps its value onto (the bounds less the thumb inset).
    const auto travel = slider.getLookAndFeel().getSliderLayout (slider).sliderBounds;
    slider.setMouseDragSensitivity (juce::jmax (1, slider.isHorizontal() ? travel.getWidth() : travel.getHeight()));
}

void Fader::resized()
{
    juce::Slider::resized();
    matchDragToTravel (*this);
}

} // namespace onebox::ui
