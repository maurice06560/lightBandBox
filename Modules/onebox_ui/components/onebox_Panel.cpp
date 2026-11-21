namespace onebox::ui
{

Panel::Panel (const juce::String& t) : title (t)
{
}

void Panel::setTitle (const juce::String& newTitle)
{
    title = newTitle;
    repaint();
}

juce::Rectangle<int> Panel::getContentBounds() const
{
    auto area = getLocalBounds();

    if (title.isNotEmpty())
        area.removeFromTop ((int) LookAndFeel::panelTitleHeight - padding / 2);

    return area.reduced (padding);
}

void Panel::paint (juce::Graphics& g)
{
    getTheme (*this).drawPanel (g, getLocalBounds().toFloat(), title);
}

} // namespace onebox::ui
