#pragma once

namespace onebox::ui
{

/** A top-level window in the OneBox theme.

    The window installs the shared OneBox LookAndFeel on itself, so every component placed inside it
    is themed without further setup, and paints the theme's background behind its content.
*/
class Window : public juce::DocumentWindow
{
public:
    /** useNativeTitleBar = false draws the title bar in the theme instead of the OS style. */
    explicit Window (const juce::String& title, bool resizable = true, bool useNativeTitleBar = true);
    ~Window() override;

    /** Shows content (owned by the window) at the given size, centred on screen. */
    void setContent (std::unique_ptr<juce::Component> content, int width, int height);

    /** The content passed to setContent(), or nullptr. */
    juce::Component* getContent() const noexcept;

    /** Called when the close button is pressed. By default the application is asked to quit. */
    std::function<void()> onCloseButtonPressed;

    void closeButtonPressed() override;

private:
    class Backdrop;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Window)
};

} // namespace onebox::ui
