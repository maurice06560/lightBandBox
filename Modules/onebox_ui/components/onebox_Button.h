#pragma once

namespace onebox::ui
{

/** A pill-shaped push button with depth.

    Variants change the resting colour: Standard (dark), Accent (blue: primary action, e.g. Play) and
    Warm (coral: destructive or record-style actions). A toggle button lights up in the accent colour
    while on.
*/
class Button : public juce::TextButton
{
public:
    enum class Variant
    {
        Standard,
        Accent,
        Warm,
        Mute,    // a toggle: dark when off, blue (accent) when on, like a console's M button
        Solo     // a toggle: dark when off, yellow when on, like a console's S button
    };

    explicit Button (const juce::String& text = {}, Variant variant = Variant::Standard, bool isToggle = false);

    void setVariant (Variant newVariant);
    Variant getVariant() const noexcept { return variant; }

    void lookAndFeelChanged() override;

private:
    void applyVariant();

    Variant variant;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Button)
};

} // namespace onebox::ui
