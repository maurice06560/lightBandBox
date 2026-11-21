namespace onebox::ui
{

IconButton::IconButton (Icon i, Variant variant, bool isToggle)
    : Button ({}, variant, isToggle), icon (i)
{
}

void IconButton::setIcon (Icon newIcon)
{
    icon = newIcon;
    repaint();
}

juce::Path IconButton::getIconPath (Icon icon)
{
    juce::Path p;

    switch (icon)
    {
        case Icon::Play:
            p.addTriangle (0.2f, 0.0f, 0.2f, 1.0f, 1.0f, 0.5f);
            break;

        case Icon::Pause:
            p.addRoundedRectangle (0.12f, 0.0f, 0.28f, 1.0f, 0.06f);
            p.addRoundedRectangle (0.60f, 0.0f, 0.28f, 1.0f, 0.06f);
            break;

        case Icon::Stop:
            p.addRoundedRectangle (0.05f, 0.05f, 0.9f, 0.9f, 0.1f);
            break;

        case Icon::Record:
            p.addEllipse (0.0f, 0.0f, 1.0f, 1.0f);
            break;

        case Icon::Plus:
            p.addRoundedRectangle (0.42f, 0.0f, 0.16f, 1.0f, 0.05f);
            p.addRoundedRectangle (0.0f, 0.42f, 1.0f, 0.16f, 0.05f);
            break;

        case Icon::Minus:
            p.addRoundedRectangle (0.0f, 0.42f, 1.0f, 0.16f, 0.05f);
            break;

        case Icon::SidebarLeft:
        case Icon::SidebarRight:
        {
            // A window outline with the panel side filled in (even-odd: frame + filled strip).
            p.addRoundedRectangle (0.0f, 0.1f, 1.0f, 0.8f, 0.12f);
            p.addRoundedRectangle (0.1f, 0.2f, 0.8f, 0.6f, 0.06f);
            p.addRectangle (icon == Icon::SidebarLeft ? 0.1f : 0.6f, 0.2f, 0.3f, 0.6f);
            p.setUsingNonZeroWinding (false);
            break;
        }

        case Icon::Metronome:
        {
            // A pyramid body with a swinging arm.
            juce::Path body;
            body.startNewSubPath (0.32f, 0.0f);
            body.lineTo (0.68f, 0.0f);
            body.lineTo (0.95f, 1.0f);
            body.lineTo (0.05f, 1.0f);
            body.closeSubPath();
            p.addPath (body);

            juce::Path arm;
            arm.addRoundedRectangle (-0.05f, -0.75f, 0.1f, 0.8f, 0.04f);
            p.addPath (arm, juce::AffineTransform::rotation (0.45f).translated (0.5f, 0.82f));
            p.setUsingNonZeroWinding (false);
            break;
        }

        case Icon::Tuner:
        {
            // A tuning fork: two prongs joined in a U, on a stem.
            juce::Path fork;
            fork.startNewSubPath (0.32f, 0.0f);
            fork.lineTo (0.32f, 0.42f);
            fork.quadraticTo (0.32f, 0.62f, 0.5f, 0.62f);
            fork.quadraticTo (0.68f, 0.62f, 0.68f, 0.42f);
            fork.lineTo (0.68f, 0.0f);
            juce::PathStrokeType (0.13f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded).createStrokedPath (p, fork);
            p.addRoundedRectangle (0.435f, 0.58f, 0.13f, 0.42f, 0.05f);
            break;
        }
    }

    return p;
}

void IconButton::paintButton (juce::Graphics& g, bool highlighted, bool down)
{
    auto& lf = getLookAndFeel();
    lf.drawButtonBackground (g, *this, findColour (getToggleState() ? buttonOnColourId : buttonColourId), highlighted, down);

    auto area = getLocalBounds().toFloat().reduced (3.0f);

    if (down)
        area.translate (0.0f, 1.0f);

    const auto size = juce::jmin (area.getWidth(), area.getHeight()) * 0.42f;
    const auto iconArea = juce::Rectangle<float> (size, size).withCentre (area.getCentre());
    auto path = getIconPath (icon);
    path.applyTransform (path.getTransformToScaleToFit (iconArea, true));

    auto colour = findColour (getToggleState() ? textColourOnId : textColourOffId);

    if (! isEnabled())
        colour = colour.withMultipliedAlpha (0.5f);

    if (icon == Icon::Record && ! getToggleState())
        colour = getTheme (*this).getPalette().accentWarm;

    g.setColour (colour);
    g.fillPath (path);
}

} // namespace onebox::ui
