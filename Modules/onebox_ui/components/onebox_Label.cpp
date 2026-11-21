namespace onebox::ui
{

Label::Label (const juce::String& text, Style s)
    : juce::Label ({}, text), style (s)
{
    applyStyle();
}

void Label::setStyle (Style newStyle)
{
    style = newStyle;
    applyStyle();
}

void Label::lookAndFeelChanged()
{
    juce::Label::lookAndFeelChanged();
    applyStyle();
}

void Label::applyStyle()
{
    getProperties().set (styleProperty, (int) style);

    const auto& palette = getTheme (*this).getPalette();
    const auto colour = [&]
    {
        switch (style)
        {
            case Style::Title:   return palette.text.brighter (0.35f);
            case Style::Heading: return palette.textDim.brighter (0.15f);
            case Style::Caption: return palette.textDim.brighter (0.25f);
            case Style::Value:   return palette.text.brighter (0.2f);
            case Style::Body:    break;
        }

        return palette.text;
    }();

    setColour (juce::Label::textColourId, colour);
    setColour (juce::Label::backgroundColourId, juce::Colours::transparentBlack);
    setColour (juce::Label::outlineColourId, juce::Colours::transparentBlack);
    setJustificationType (style == Style::Value ? juce::Justification::centred : juce::Justification::centredLeft);
    setBorderSize (style == Style::Value ? juce::BorderSize<int> (2, 8, 2, 8) : juce::BorderSize<int> (1, 2, 1, 2));
    repaint();
}

} // namespace onebox::ui
