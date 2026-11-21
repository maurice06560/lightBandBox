#pragma once

namespace onebox::ui
{

/** A text label in one of the theme's five styles. */
class Label : public juce::Label
{
public:
    enum class Style
    {
        Title,      // large and bold: window or section titles
        Heading,    // small caps, dimmed, widely spaced: like panel titles
        Body,       // regular text
        Caption,    // small and dimmed: hints, units, secondary info
        Value       // a value in a recessed readout box, e.g. "120 BPM", "-6.0 dB"
    };

    /** The component property that carries the style to the LookAndFeel. */
    static constexpr const char* styleProperty = "onebox.labelStyle";

    explicit Label (const juce::String& text = {}, Style style = Style::Body);

    void setStyle (Style newStyle);
    Style getStyle() const noexcept { return style; }

    void lookAndFeelChanged() override;

private:
    void applyStyle();

    Style style;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Label)
};

} // namespace onebox::ui
