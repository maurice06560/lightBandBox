namespace onebox::ui
{

/** Paints the theme background and keeps the user's content filling it. */
class Window::Backdrop final : public juce::Component
{
public:
    explicit Backdrop (std::unique_ptr<juce::Component> c) : content (std::move (c))
    {
        if (content != nullptr)
            addAndMakeVisible (*content);
    }

    void paint (juce::Graphics& g) override  { getTheme (*this).drawBackground (g, getLocalBounds().toFloat()); }
    void resized() override                  { if (content != nullptr) content->setBounds (getLocalBounds()); }

    juce::Component* getContent() const noexcept { return content.get(); }

private:
    std::unique_ptr<juce::Component> content;
};

//==============================================================================
Window::Window (const juce::String& title, bool resizable, bool useNativeTitleBar)
    : juce::DocumentWindow (title, LookAndFeel::getShared().getPalette().bgBottom, juce::DocumentWindow::allButtons)
{
    setLookAndFeel (&LookAndFeel::getShared());
    setUsingNativeTitleBar (useNativeTitleBar);
    setResizable (resizable, false);
}

Window::~Window()
{
    // Content goes first, while the theme it was drawn with is still installed.
    clearContentComponent();
    setLookAndFeel (nullptr);
}

void Window::setContent (std::unique_ptr<juce::Component> content, int width, int height)
{
    auto backdrop = std::make_unique<Backdrop> (std::move (content));
    backdrop->setSize (width, height);
    setContentOwned (backdrop.release(), true);
    centreWithSize (getWidth(), getHeight());
}

juce::Component* Window::getContent() const noexcept
{
    if (auto* backdrop = dynamic_cast<Backdrop*> (getContentComponent()))
        return backdrop->getContent();

    return nullptr;
}

void Window::closeButtonPressed()
{
    if (onCloseButtonPressed != nullptr)
        onCloseButtonPressed();
    else if (auto* app = juce::JUCEApplicationBase::getInstance())
        app->systemRequestedQuit();
}

} // namespace onebox::ui
