namespace onebox::ui
{

SegmentedControl::SegmentedControl (const juce::StringArray& s) : segments (s)
{
}

void SegmentedControl::setSegments (const juce::StringArray& newSegments)
{
    segments = newSegments;
    selected = juce::jlimit (0, juce::jmax (0, segments.size() - 1), selected);
    repaint();
}

void SegmentedControl::setSelectedIndex (int index, juce::NotificationType notification)
{
    index = juce::jlimit (0, juce::jmax (0, segments.size() - 1), index);

    if (index == selected)
        return;

    selected = index;
    repaint();

    if (notification != juce::dontSendNotification && onChange != nullptr)
        onChange (selected);
}

juce::Rectangle<float> SegmentedControl::segmentBounds (int index) const
{
    const auto area = getLocalBounds().toFloat().reduced (3.0f);
    const auto width = area.getWidth() / (float) juce::jmax (1, segments.size());
    return { area.getX() + width * (float) index, area.getY(), width, area.getHeight() };
}

int SegmentedControl::segmentAt (juce::Point<int> position) const
{
    for (int i = 0; i < segments.size(); ++i)
        if (segmentBounds (i).contains (position.toFloat()))
            return i;

    return -1;
}

void SegmentedControl::paint (juce::Graphics& g)
{
    auto& theme = getTheme (*this);
    const auto& palette = theme.getPalette();

    // Disabled: the whole control fades, selected pill included.
    if (! isEnabled())
        g.beginTransparencyLayer (0.45f);
    const auto area = getLocalBounds().toFloat().reduced (0.5f);
    const auto corner = juce::jmin (8.0f, area.getHeight() * 0.3f);

    // recessed track
    g.setColour (palette.arcTrack);
    g.fillRoundedRectangle (area, corner);
    g.setGradientFill (juce::ColourGradient (juce::Colours::black.withAlpha (0.45f), 0.0f, area.getY(),
                                             juce::Colours::transparentBlack, 0.0f, area.getY() + 6.0f, false));
    g.fillRoundedRectangle (area, corner);
    g.setColour (palette.panelBorder);
    g.drawRoundedRectangle (area, corner, 1.0f);

    g.setFont (LookAndFeel::makeFont (juce::jlimit (11.0f, 16.0f, (float) getHeight() * 0.42f), true)
                   .withExtraKerningFactor (0.06f));

    for (int i = 0; i < segments.size(); ++i)
    {
        const auto bounds = segmentBounds (i);

        if (i == selected)
        {
            const auto pill = bounds.reduced (1.0f);
            juce::Path shape;
            shape.addRoundedRectangle (pill, corner - 2.0f);
            juce::DropShadow (juce::Colours::black.withAlpha (0.5f), 6, { 0, 2 }).drawForPath (g, shape);

            g.setGradientFill (juce::ColourGradient (palette.accent.brighter (0.12f), 0.0f, pill.getY(),
                                                     palette.accent.darker (0.25f), 0.0f, pill.getBottom(), false));
            g.fillPath (shape);
            g.setColour (juce::Colours::white.withAlpha (0.2f));
            g.drawRoundedRectangle (pill.reduced (0.5f), corner - 2.0f, 1.0f);
        }
        else
        {
            if (i == hovered)
            {
                g.setColour (juce::Colours::white.withAlpha (0.04f));
                g.fillRoundedRectangle (bounds.reduced (1.0f), corner - 2.0f);
            }

            // separator between two unselected segments
            if (i > 0 && i - 1 != selected)
            {
                g.setColour (palette.panelBorder);
                g.fillRect (bounds.getX() - 0.5f, bounds.getY() + bounds.getHeight() * 0.25f, 1.0f, bounds.getHeight() * 0.5f);
            }
        }

        g.setColour (i == selected ? palette.textOnAccent : palette.text);
        g.drawFittedText (segments[i], bounds.toNearestInt().reduced (4, 0), juce::Justification::centred, 1, 0.8f);
    }

    if (! isEnabled())
        g.endTransparencyLayer();
}

void SegmentedControl::mouseDown (const juce::MouseEvent& e)
{
    if (! isEnabled())
        return;

    if (const auto index = segmentAt (e.getPosition()); index >= 0)
        setSelectedIndex (index);
}

void SegmentedControl::mouseMove (const juce::MouseEvent& e)
{
    if (const auto index = segmentAt (e.getPosition()); index != hovered)
    {
        hovered = index;
        repaint();
    }
}

void SegmentedControl::mouseExit (const juce::MouseEvent&)
{
    hovered = -1;
    repaint();
}

} // namespace onebox::ui
