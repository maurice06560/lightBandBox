namespace onebox::ui
{

Button::Button (const juce::String& text, Variant v, bool isToggle)
    : juce::TextButton (text), variant (v)
{
    setClickingTogglesState (isToggle);

    // Like hardware and DAW buttons: clicking doesn't steal the keyboard, so app shortcuts
    // (e.g. space for play) keep working after a click.
    setWantsKeyboardFocus (false);
    applyVariant();
}

void Button::setVariant (Variant newVariant)
{
    variant = newVariant;
    applyVariant();
}

void Button::lookAndFeelChanged()
{
    juce::TextButton::lookAndFeelChanged();
    applyVariant();
}

void Button::applyVariant()
{
    const auto& palette = getTheme (*this).getPalette();

    switch (variant)
    {
        case Variant::Standard:
            setColour (buttonColourId,   palette.capBottom);
            setColour (buttonOnColourId, palette.accent);
            setColour (textColourOffId,  palette.text);
            break;

        case Variant::Accent:
            setColour (buttonColourId,   palette.accent);
            setColour (buttonOnColourId, palette.accent.brighter (0.25f));
            setColour (textColourOffId,  palette.textOnAccent);
            break;

        case Variant::Warm:
            setColour (buttonColourId,   palette.accentWarm);
            setColour (buttonOnColourId, palette.accentWarm.brighter (0.25f));
            setColour (textColourOffId,  palette.textOnAccent);
            break;

        case Variant::Mute:
            setColour (buttonColourId,   palette.capBottom);
            setColour (buttonOnColourId, palette.accent);
            setColour (textColourOffId,  palette.text);
            break;

        case Variant::Solo:
            setColour (buttonColourId,   palette.capBottom);
            setColour (buttonOnColourId, palette.solo);
            setColour (textColourOffId,  palette.text);
            break;
    }

    setColour (textColourOnId, palette.textOnAccent);
    repaint();
}

} // namespace onebox::ui
