#pragma once

namespace onebox::ui
{

/** The OneBox theme: a pro-style dark LookAndFeel, entirely vector-drawn.

    - Layered "3D" rotary knobs: drop shadow, lit bezel, gradient cap, glowing value arc and a rotating
      indicator dot (the lighting stays fixed). The knob's name is drawn in the cap and turns into the
      live value on hover/drag. Bipolar knobs (arc grows from 12 o'clock): properties "bipolar" = true.
    - Vertical linear sliders draw as a console fader cap; horizontal ones as a rounded track and thumb.
    - Pill buttons with depth, labels in five styles, recessed panels, gradient + grain backgrounds.
    - ChannelFader (fader + ruler + meter) is drawn through its LookAndFeelMethods.

    Components in this module find the theme automatically inside a ui::Window. Anything else can use
    getShared(), or its own instance.
*/
class LookAndFeel : public juce::LookAndFeel_V4,
                    public ChannelFader::LookAndFeelMethods
{
public:
    //==============================================================================
    struct Palette
    {
        juce::Colour bgTop        { 0xff222840 };
        juce::Colour bgBottom     { 0xff11141e };
        juce::Colour panelFill    { 0xff181c2b };
        juce::Colour panelBorder  { 0xff2c3247 };
        juce::Colour bezelTop     { 0xff353c52 };
        juce::Colour bezelBottom  { 0xff121520 };
        juce::Colour capTop       { 0xff3c4459 };
        juce::Colour capBottom    { 0xff1f2435 };
        juce::Colour arcTrack     { 0xff0c0e16 };
        juce::Colour accent       { 0xff5fb6ee };   // blue
        juce::Colour accentWarm   { 0xffe0786a };   // coral (output / mix / record style)
        juce::Colour solo         { 0xffe9c046 };   // yellow, for solo buttons
        juce::Colour text         { 0xff9fc4e4 };
        juce::Colour textDim      { 0xff5d6a85 };
        juce::Colour textOnAccent { 0xff0d1018 };
    };

    LookAndFeel();
    ~LookAndFeel() override = default;

    /** One application-wide instance, deleted automatically at shutdown (DeletedAtShutdown). */
    static LookAndFeel& getShared();

    const Palette& getPalette() const noexcept { return palette; }

    //==============================================================================
    // Sliders
    void drawRotarySlider (juce::Graphics&, int x, int y, int width, int height,
                           float sliderPosProportional, float rotaryStartAngle,
                           float rotaryEndAngle, juce::Slider&) override;

    void drawLinearSlider (juce::Graphics&, int x, int y, int width, int height,
                           float sliderPos, float minSliderPos, float maxSliderPos,
                           juce::Slider::SliderStyle, juce::Slider&) override;

    int getSliderThumbRadius (juce::Slider&) override;

    // ChannelFader
    void drawChannelFaderBackground (juce::Graphics&, juce::Rectangle<float> area, ChannelFader&) override;
    void drawChannelFaderReadout    (juce::Graphics&, juce::Rectangle<float> area, bool highlighted, ChannelFader&) override;
    void drawChannelFaderRuler      (juce::Graphics&, juce::Rectangle<float> area, ChannelFader&) override;
    void drawChannelFaderMeter      (juce::Graphics&, juce::Rectangle<float> scaleArea,
                                     juce::Rectangle<float> meterArea, ChannelFader&) override;

    // Buttons
    void drawButtonBackground (juce::Graphics&, juce::Button&, const juce::Colour& backgroundColour,
                               bool shouldDrawButtonAsHighlighted, bool shouldDrawButtonAsDown) override;
    void drawButtonText (juce::Graphics&, juce::TextButton&,
                         bool shouldDrawButtonAsHighlighted, bool shouldDrawButtonAsDown) override;
    juce::Font getTextButtonFont (juce::TextButton&, int buttonHeight) override;

    // Labels (styles set by ui::Label; plain juce::Labels draw as Body)
    void drawLabel (juce::Graphics&, juce::Label&) override;
    juce::Font getLabelFont (juce::Label&) override;

    // Tables
    void drawTableHeaderBackground (juce::Graphics&, juce::TableHeaderComponent&) override;
    void drawTableHeaderColumn (juce::Graphics&, juce::TableHeaderComponent&, const juce::String& columnName,
                                int columnId, int width, int height, bool isMouseOver,
                                bool isMouseDown, int columnFlags) override;

    // Alert windows: the panel background with an accent stripe (coral for warnings) instead of
    // a large icon, and the theme's fonts
    void drawAlertBox (juce::Graphics&, juce::AlertWindow&, const juce::Rectangle<int>& textArea, juce::TextLayout&) override;
    int getAlertWindowButtonHeight() override  { return 32; }
    juce::Font getAlertWindowTitleFont() override;
    juce::Font getAlertWindowMessageFont() override;
    juce::Font getAlertWindowFont() override;

    // Windows
    void drawDocumentWindowTitleBar (juce::DocumentWindow&, juce::Graphics&, int width, int height,
                                     int titleSpaceX, int titleSpaceW, const juce::Image* icon,
                                     bool drawTitleTextOnLeft) override;

    //==============================================================================
    // Helpers for your own paint() methods

    /** Gradient + soft light pool + vignette + fine noise grain. */
    void drawBackground (juce::Graphics&, juce::Rectangle<float> area);

    /** Recessed rounded panel to group controls, with an optional small-caps title. */
    void drawPanel (juce::Graphics&, juce::Rectangle<float> area, const juce::String& title = {});

    /** Recessed "LCD" box used for numeric readouts. */
    void drawReadout (juce::Graphics&, juce::Rectangle<float> area, bool highlighted);

    /** Sets up a slider as a knob: rotary, no text box, 270-degree sweep, name in the cap. */
    static void styleKnob (juce::Slider&, const juce::String& name, bool bipolar = false);

    /** The theme's font. Change the family here (or load an embedded one). */
    static juce::Font makeFont (float height, bool bold = false);

    /** Height of the title area drawPanel() reserves when given a title. */
    static constexpr float panelTitleHeight = 28.0f;

private:
    void drawVerticalFader (juce::Graphics&, juce::Rectangle<float> travel, float sliderPos, juce::Slider&);

    Palette palette;
    juce::Image noiseTile;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (LookAndFeel)
};

/** The OneBox theme a component should draw with: its own LookAndFeel when that is a OneBox theme,
    otherwise the shared one.
*/
LookAndFeel& getTheme (juce::Component&);

} // namespace onebox::ui
