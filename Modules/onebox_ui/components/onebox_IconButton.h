#pragma once

namespace onebox::ui
{

/** A pill button showing a vector icon instead of text: transport controls and small +/- actions. */
class IconButton : public Button
{
public:
    enum class Icon
    {
        Play,
        Pause,
        Stop,
        Record,
        Plus,
        Minus,
        Metronome,
        Tuner,          // a tuning fork
        SidebarLeft,    // show/hide a left-hand panel
        SidebarRight    // show/hide a right-hand panel
    };

    explicit IconButton (Icon icon, Variant variant = Variant::Standard, bool isToggle = false);

    void setIcon (Icon newIcon);
    Icon getIcon() const noexcept { return icon; }

    /** The icon's outline in a unit square (0..1), for reuse elsewhere. */
    static juce::Path getIconPath (Icon icon);

    void paintButton (juce::Graphics&, bool shouldDrawButtonAsHighlighted, bool shouldDrawButtonAsDown) override;

private:
    Icon icon;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (IconButton)
};

} // namespace onebox::ui
