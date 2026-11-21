namespace onebox::ui
{

using namespace juce;


//==============================================================================
LookAndFeel::LookAndFeel()
{
    // The V4 colour scheme themes every stock JUCE widget (menus, combo boxes, text editors,
    // scrollbars, alert windows...). It must come first: it resets all colours.
    setColourScheme ({ palette.bgBottom,      // windowBackground
                       palette.panelFill,     // widgetBackground
                       palette.panelFill,     // menuBackground
                       palette.panelBorder,   // outline
                       palette.text,          // defaultText
                       palette.capBottom,     // defaultFill
                       palette.textOnAccent,  // highlightedText
                       palette.accent,        // highlightedFill
                       palette.text });       // menuText

    // Default colours so un-customised components still match the theme
    setColour (ResizableWindow::backgroundColourId,        palette.bgBottom);
    setColour (Slider::rotarySliderFillColourId,           palette.accent);
    setColour (Slider::rotarySliderOutlineColourId,        palette.arcTrack);
    setColour (Slider::thumbColourId,                      palette.accent);
    setColour (Slider::textBoxTextColourId,                palette.text);
    setColour (Slider::textBoxOutlineColourId,             Colours::transparentBlack);
    setColour (juce::Label::textColourId,                  palette.text);
    setColour (TextButton::buttonColourId,                 palette.capBottom);
    setColour (TextButton::buttonOnColourId,               palette.accent);
    setColour (TextButton::textColourOffId,                palette.text);
    setColour (TextButton::textColourOnId,                 palette.textOnAccent);
    setColour (DocumentWindow::textColourId,               palette.text);
    setColour (TooltipWindow::backgroundColourId,          palette.panelFill);
    setColour (TooltipWindow::textColourId,                palette.text);
    setColour (TooltipWindow::outlineColourId,             palette.panelBorder);
    setColour (TextEditor::backgroundColourId,             palette.arcTrack);
    setColour (TextEditor::outlineColourId,                palette.panelBorder);
    setColour (TextEditor::focusedOutlineColourId,         palette.accent);
    setColour (TextEditor::textColourId,                   palette.text.brighter (0.3f));
    setColour (TextEditor::highlightColourId,              palette.accent.withAlpha (0.35f));
    setColour (CaretComponent::caretColourId,              palette.accent);
    setColour (ComboBox::backgroundColourId,               palette.capBottom);
    setColour (ComboBox::textColourId,                     palette.text);
    setColour (ComboBox::arrowColourId,                    palette.textDim.brighter (0.4f));
    setColour (ComboBox::focusedOutlineColourId,           palette.accent);
    setColour (ListBox::backgroundColourId,                Colours::transparentBlack);
    setColour (ListBox::outlineColourId,                   Colours::transparentBlack);
    setColour (TableHeaderComponent::textColourId,         palette.textDim.brighter (0.3f));
    setColour (TableHeaderComponent::backgroundColourId,   palette.panelFill);
    setColour (TableHeaderComponent::outlineColourId,      palette.panelBorder);
    setColour (TableHeaderComponent::highlightColourId,    palette.capBottom);
    setColour (ScrollBar::thumbColourId,                   palette.textDim.withAlpha (0.6f));
    setColour (AlertWindow::backgroundColourId,            palette.panelFill);
    setColour (AlertWindow::textColourId,                  palette.text);
    setColour (AlertWindow::outlineColourId,               palette.panelBorder);
    setColour (PopupMenu::highlightedBackgroundColourId,   palette.accent.withAlpha (0.85f));
    setColour (PopupMenu::highlightedTextColourId,         palette.textOnAccent);
    setColour (ComboBox::outlineColourId,                  palette.panelBorder);
    setColour (PopupMenu::backgroundColourId,              palette.panelFill);

    // Pre-render a small noise tile for background grain (generated once)
    noiseTile = Image (Image::ARGB, 128, 128, true);
    Random rng (0x5eed);
    for (int y = 0; y < noiseTile.getHeight(); ++y)
        for (int x = 0; x < noiseTile.getWidth(); ++x)
            noiseTile.setPixelAt (x, y, Colours::white.withAlpha ((uint8) rng.nextInt (9)));
}

//==============================================================================
Font LookAndFeel::makeFont (float height, bool bold)
{
    // "Avenir Next" ships with macOS. To embed your own .ttf, add it to
    // BinaryData and use Typeface::createSystemTypefaceFor(...) instead.
   #if JUCE_MAJOR_VERSION >= 8
    return Font (FontOptions ("Avenir Next", height, bold ? Font::bold : Font::plain));
   #else
    return Font ("Avenir Next", height, bold ? Font::bold : Font::plain);
   #endif
}

void LookAndFeel::styleKnob (Slider& s, const String& name, bool bipolar)
{
    s.setName (name);
    s.setSliderStyle (Slider::RotaryHorizontalVerticalDrag);
    s.setTextBoxStyle (Slider::NoTextBox, false, 0, 0);
    s.setRotaryParameters (MathConstants<float>::pi * 1.25f,   // 7:30
                           MathConstants<float>::pi * 2.75f,   // 4:30
                           true);
    s.setMouseDragSensitivity (220);
    s.getProperties().set ("bipolar", bipolar);
}

//==============================================================================
void LookAndFeel::drawRotarySlider (Graphics& g, int x, int y, int width, int height,
                                          float sliderPos, float rotaryStartAngle,
                                          float rotaryEndAngle, Slider& slider)
{
    const bool enabled = slider.isEnabled();
    const bool active  = slider.isMouseOverOrDragging();

    const auto bounds = Rectangle<int> (x, y, width, height).toFloat();
    const float size  = jmin (bounds.getWidth(), bounds.getHeight());
    if (size < 8.0f)
        return;

    const auto area   = bounds.withSizeKeepingCentre (size, size).reduced (size * 0.03f);
    const auto centre = area.getCentre();
    const float angle = rotaryStartAngle + sliderPos * (rotaryEndAngle - rotaryStartAngle);

    // Geometry: [ value arc ] gap [ bezel ] [ cap ]
    const float arcThickness = jmax (2.0f, size * 0.03f);
    const float arcRadius    = area.getWidth() * 0.5f - arcThickness;
    const auto  bezel        = area.reduced (size * 0.11f);
    const auto  cap          = bezel.reduced (bezel.getWidth() * 0.075f);

    auto accent = slider.findColour (Slider::rotarySliderFillColourId);
    if (! enabled)
        accent = accent.withSaturation (0.0f).withMultipliedAlpha (0.5f);

    //--------------------------------------------------------------------------
    // 1. Value-arc track (recessed groove)
    {
        Path track;
        track.addCentredArc (centre.x, centre.y, arcRadius, arcRadius, 0.0f,
                             rotaryStartAngle, rotaryEndAngle, true);
        g.setColour (slider.findColour (Slider::rotarySliderOutlineColourId).withAlpha (0.9f));
        g.strokePath (track, PathStrokeType (arcThickness, PathStrokeType::curved, PathStrokeType::rounded));
    }

    // 2. Value arc with glow
    {
        const bool bipolar   = (bool) slider.getProperties().getWithDefault ("bipolar", false);
        const float fromAngle = bipolar ? (rotaryStartAngle + rotaryEndAngle) * 0.5f : rotaryStartAngle;

        if (std::abs (angle - fromAngle) > 0.001f)
        {
            Path valueArc;
            valueArc.addCentredArc (centre.x, centre.y, arcRadius, arcRadius, 0.0f,
                                    jmin (fromAngle, angle), jmax (fromAngle, angle), true);

            // soft glow passes, wide -> narrow
            for (int i = 3; i >= 1; --i)
            {
                g.setColour (accent.withMultipliedAlpha (0.07f * (float) (4 - i) * (active ? 1.4f : 1.0f)));
                g.strokePath (valueArc, PathStrokeType (arcThickness * (1.0f + (float) i * 1.4f),
                                                        PathStrokeType::curved, PathStrokeType::rounded));
            }

            g.setColour (accent);
            g.strokePath (valueArc, PathStrokeType (arcThickness, PathStrokeType::curved, PathStrokeType::rounded));
        }
    }

    //--------------------------------------------------------------------------
    // 3. Drop shadow under the knob body (light comes from top-left)
    {
        Path body;
        body.addEllipse (bezel);
        DropShadow (Colours::black.withAlpha (0.65f),
                    roundToInt (size * 0.10f),
                    { roundToInt (size * 0.02f), roundToInt (size * 0.05f) }).drawForPath (g, body);
    }

    // 4. Bezel (outer ring): lit from top, dark at bottom
    g.setGradientFill (ColourGradient (palette.bezelTop,    bezel.getX(),     bezel.getY(),
                                       palette.bezelBottom, bezel.getRight(), bezel.getBottom(), false));
    g.fillEllipse (bezel);

    // thin specular rim on the bezel
    g.setGradientFill (ColourGradient (Colours::white.withAlpha (0.16f), bezel.getCentreX(), bezel.getY(),
                                       Colours::white.withAlpha (0.0f),  bezel.getCentreX(), bezel.getCentreY(), false));
    g.drawEllipse (bezel.reduced (0.75f), 1.5f);

    // 5. Inner groove between bezel and cap (fake inner shadow)
    g.setColour (Colours::black.withAlpha (0.45f));
    g.drawEllipse (cap.expanded (1.0f), 2.0f);

    // 6. Cap: diagonal gradient + radial sheen
    auto capTop = palette.capTop;
    if (active && enabled)
        capTop = capTop.brighter (0.12f);

    g.setGradientFill (ColourGradient (capTop,            cap.getX(),     cap.getY(),
                                       palette.capBottom, cap.getRight(), cap.getBottom(), false));
    g.fillEllipse (cap);

    {
        const auto sheenCentre = cap.getCentre().translated (-cap.getWidth() * 0.18f, -cap.getHeight() * 0.22f);
        ColourGradient sheen (Colours::white.withAlpha (0.10f), sheenCentre.x, sheenCentre.y,
                              Colours::white.withAlpha (0.0f),  sheenCentre.x + cap.getWidth() * 0.6f, sheenCentre.y, true);
        g.setGradientFill (sheen);
        g.fillEllipse (cap);
    }

    // top rim highlight on the cap
    g.setGradientFill (ColourGradient (Colours::white.withAlpha (0.12f), cap.getCentreX(), cap.getY(),
                                       Colours::white.withAlpha (0.0f),  cap.getCentreX(), cap.getCentreY(), false));
    g.drawEllipse (cap.reduced (0.5f), 1.0f);

    //--------------------------------------------------------------------------
    // 7. Indicator dot: only this rotates, so the lighting never moves
    {
        const float dotRadius = jmax (2.0f, size * 0.026f);
        const auto  dotPos    = centre.getPointOnCircumference (cap.getWidth() * 0.5f * 0.78f, angle);

        g.setColour (accent.withMultipliedAlpha (0.30f));
        g.fillEllipse (Rectangle<float> (dotRadius * 4.0f, dotRadius * 4.0f).withCentre (dotPos));

        g.setColour (accent.brighter (0.3f));
        g.fillEllipse (Rectangle<float> (dotRadius * 2.0f, dotRadius * 2.0f).withCentre (dotPos));
    }

    //--------------------------------------------------------------------------
    // 8. Label inside the cap: name normally, live value on hover/drag
    const auto name = slider.getName();
    if (name.isNotEmpty() || active)
    {
        const String text = active ? slider.getTextFromValue (slider.getValue())
                                   : name.toUpperCase();

        const auto textArea = cap.reduced (cap.getWidth() * 0.16f);
        g.setFont (makeFont (jlimit (9.0f, 26.0f, cap.getHeight() * 0.15f))
                       .withExtraKerningFactor (0.08f));
        g.setColour ((active ? palette.text.brighter (0.4f) : palette.text)
                         .withMultipliedAlpha (enabled ? 1.0f : 0.4f));
        g.drawFittedText (text, textArea.toNearestInt(), Justification::centred, 2, 0.75f);
    }
}

//==============================================================================
void LookAndFeel::drawLinearSlider (Graphics& g, int x, int y, int width, int height,
                                          float sliderPos, float /* minSliderPos */, float /* maxSliderPos */,
                                          const Slider::SliderStyle style, Slider& slider)
{
    if (style == Slider::LinearVertical)
    {
        drawVerticalFader (g, Rectangle<int> (x, y, width, height).toFloat(), sliderPos, slider);
        return;
    }

    const bool enabled = slider.isEnabled();
    const bool active  = slider.isMouseOverOrDragging();

    const auto bounds = Rectangle<int> (x, y, width, height).toFloat();
    const float trackHeight = jmin (24.0f, bounds.getHeight() * 0.6f);
    const auto trackBounds = bounds.withSizeKeepingCentre (bounds.getWidth() - 12.0f, trackHeight);
    const float corner = trackHeight * 0.5f;

    auto accent = slider.findColour (Slider::rotarySliderFillColourId);
    if (!enabled)
        accent = accent.withSaturation (0.0f).withMultipliedAlpha (0.5f);

    // Track background (recessed groove)
    g.setColour (palette.arcTrack.withAlpha (0.9f));
    g.fillRoundedRectangle (trackBounds, corner);

    // Track border
    g.setColour (palette.panelBorder);
    g.drawRoundedRectangle (trackBounds, corner, 1.0f);

    // Value fill (blue curved top)
    if (style == Slider::LinearHorizontal)
    {
        const float fillWidth = (sliderPos - trackBounds.getX());
        if (fillWidth > 0.0f)
        {
            auto fillBounds = trackBounds.withWidth (fillWidth);

            // Gradient fill for the value track
            ColourGradient grad (palette.capTop.brighter (0.2f), fillBounds.getX(), fillBounds.getY(),
                                 palette.capBottom.brighter (0.1f), fillBounds.getX(), fillBounds.getBottom(), false);
            g.setGradientFill (grad);
            g.fillRoundedRectangle (fillBounds, corner);

            // Glow passes for active state
            if (active && enabled)
            {
                for (int i = 3; i >= 1; --i)
                {
                    const float expandAmount = (float)i * 1.5f;
                    g.setColour (accent.withMultipliedAlpha (0.06f * (float)(4 - i)));
                    g.fillRoundedRectangle (fillBounds.expanded (expandAmount, 0.0f), corner + expandAmount);
                }
            }

            // Accent color on top
            g.setColour (accent.brighter (active ? 0.15f : 0.0f));
            g.fillRoundedRectangle (fillBounds, corner);
        }
    }

    // Thumb
    const float thumbWidth = 14.0f;
    const float thumbHeight = trackHeight + 8.0f;
    const auto thumbBounds = Rectangle<float> (thumbWidth, thumbHeight)
                                .withCentre (Point<float> (sliderPos, trackBounds.getCentreY()));

    // Thumb shadow
    Path thumbShape;
    thumbShape.addRoundedRectangle (thumbBounds, 4.0f);
    DropShadow (Colours::black.withAlpha (0.55f), 8, {0, 2}).drawForPath (g, thumbShape);

    // Thumb body gradient
    Colour thumbColour = palette.capBottom.brighter (active ? 0.2f : 0.0f);
    g.setGradientFill (ColourGradient (thumbColour.brighter (0.15f), thumbBounds.getX(), thumbBounds.getY(),
                                       thumbColour.darker (0.15f), thumbBounds.getX(), thumbBounds.getBottom(), false));
    g.fillRoundedRectangle (thumbBounds, 4.0f);

    // Thumb highlight
    g.setColour (Colours::white.withAlpha (0.15f));
    g.drawRoundedRectangle (thumbBounds.reduced (0.5f), 4.0f, 1.0f);
}

//==============================================================================
namespace
{
    // Console fader cap: taller than wide, sized from the slider's width.
    Point<float> faderCapSize (float sliderWidth)
    {
        const float w = jlimit (18.0f, 40.0f, sliderWidth * 0.6f);
        return { w, w * 1.6f };
    }
}

int LookAndFeel::getSliderThumbRadius (Slider& slider)
{
    // Inset the travel by half the cap so the cap never leaves the component
    if (slider.getSliderStyle() == Slider::LinearVertical)
        return roundToInt (faderCapSize ((float) slider.getWidth()).y * 0.5f) + 2;

    return LookAndFeel_V4::getSliderThumbRadius (slider);
}

void LookAndFeel::drawVerticalFader (Graphics& g, Rectangle<float> travel,
                                           float sliderPos, Slider& slider)
{
    const bool enabled = slider.isEnabled();
    const bool active  = slider.isMouseOverOrDragging();

    auto accent = slider.findColour (Slider::rotarySliderFillColourId);
    if (! enabled)
        accent = accent.withSaturation (0.0f).withMultipliedAlpha (0.5f);

    const float cx = travel.getCentreX();

    //--------------------------------------------------------------------------
    // 1. Slot the cap rides in (recessed groove)
    const auto slot = Rectangle<float> (6.0f, travel.getHeight() + 12.0f).withCentre ({ cx, travel.getCentreY() });
    g.setColour (palette.arcTrack.withAlpha (0.95f));
    g.fillRoundedRectangle (slot, 3.0f);
    g.setColour (Colours::black.withAlpha (0.5f));
    g.fillRect (slot.withWidth (1.5f).reduced (0.0f, 3.0f));   // inner shadow on the left wall
    g.setColour (palette.panelBorder);
    g.drawRoundedRectangle (slot, 3.0f, 1.0f);

    // 2. Value line from the bottom up to the cap, with glow
    {
        const auto fill = Rectangle<float> (cx - 1.0f, sliderPos, 2.0f, slot.getBottom() - 3.0f - sliderPos);
        if (fill.getHeight() > 0.0f)
        {
            for (int i = 3; i >= 1; --i)
            {
                g.setColour (accent.withMultipliedAlpha (0.07f * (float) (4 - i) * (active ? 1.4f : 1.0f)));
                g.fillRoundedRectangle (fill.expanded ((float) i * 1.2f, 0.0f), 2.0f);
            }
            g.setColour (accent.withMultipliedAlpha (0.85f));
            g.fillRoundedRectangle (fill, 1.0f);
        }
    }

    //--------------------------------------------------------------------------
    // 3. Cap
    const auto capSize = faderCapSize (travel.getWidth());
    const auto cap     = Rectangle<float> (capSize.x, capSize.y).withCentre ({ cx, sliderPos });
    const float corner = 4.0f;

    Path capShape;
    capShape.addRoundedRectangle (cap, corner);
    DropShadow (Colours::black.withAlpha (0.65f), 12, { 1, 5 }).drawForPath (g, capShape);

    // body: lit top half, dark ridge in the middle, shaded bottom half
    auto capTop    = palette.capTop;
    auto capBottom = palette.capBottom;
    if (active && enabled)
    {
        capTop    = capTop.brighter (0.12f);
        capBottom = capBottom.brighter (0.08f);
    }

    ColourGradient body (capTop.brighter (0.25f), cx, cap.getY(),
                         capBottom.darker (0.25f), cx, cap.getBottom(), false);
    body.addColour (0.46, capBottom);
    body.addColour (0.54, capTop.brighter (0.05f));
    g.setGradientFill (body);
    g.fillPath (capShape);

    // side bevel: light from the left
    g.setGradientFill (ColourGradient (Colours::white.withAlpha (0.07f), cap.getX(),     cap.getCentreY(),
                                       Colours::black.withAlpha (0.18f), cap.getRight(), cap.getCentreY(), false));
    g.fillPath (capShape);

    // engraved grip lines above and below the centre
    {
        const float spacing = cap.getHeight() / 10.0f;
        const auto  lines   = cap.reduced (cap.getWidth() * 0.14f, 0.0f);

        for (int i = 1; i <= 3; ++i)
        {
            for (float dir : { -1.0f, 1.0f })
            {
                const float ly = cap.getCentreY() + dir * spacing * (float) i;
                g.setColour (Colours::black.withAlpha (0.40f));
                g.fillRect (lines.getX(), ly - 0.5f, lines.getWidth(), 1.0f);
                g.setColour (Colours::white.withAlpha (0.09f));
                g.fillRect (lines.getX(), ly + 0.5f, lines.getWidth(), 1.0f);
            }
        }
    }

    // glowing centre indicator (the value line)
    {
        const auto line = Rectangle<float> (cap.getWidth() - 6.0f, 2.0f).withCentre (cap.getCentre());
        g.setColour (accent.withMultipliedAlpha (active ? 0.45f : 0.30f));
        g.fillRoundedRectangle (line.expanded (1.0f, 2.0f), 2.0f);
        g.setColour (accent.brighter (active ? 0.4f : 0.2f));
        g.fillRoundedRectangle (line, 1.0f);
    }

    // top highlight + dark outline
    g.setGradientFill (ColourGradient (Colours::white.withAlpha (0.22f), cx, cap.getY(),
                                       Colours::white.withAlpha (0.0f),  cx, cap.getY() + cap.getHeight() * 0.3f, false));
    g.drawRoundedRectangle (cap.reduced (0.75f), corner, 1.0f);

    g.setColour (Colours::black.withAlpha (0.6f));
    g.drawRoundedRectangle (cap, corner, 1.0f);
}

//==============================================================================
void LookAndFeel::drawChannelFaderBackground (Graphics& g, Rectangle<float> area, ChannelFader&)
{
    drawPanel (g, area);
}

void LookAndFeel::drawChannelFaderReadout (Graphics& g, Rectangle<float> area,
                                           bool highlighted, ChannelFader&)
{
    drawReadout (g, area, highlighted);
}

void LookAndFeel::drawReadout (Graphics& g, Rectangle<float> area, bool highlighted)
{
    const float corner = 4.0f;

    g.setColour (palette.arcTrack);
    g.fillRoundedRectangle (area, corner);

    // inner shadow at the top edge
    g.setGradientFill (ColourGradient (Colours::black.withAlpha (0.5f), area.getX(), area.getY(),
                                       Colours::transparentBlack,       area.getX(), area.getY() + 6.0f, false));
    g.fillRoundedRectangle (area, corner);

    g.setColour (highlighted ? palette.accent : palette.panelBorder);
    g.drawRoundedRectangle (area.reduced (0.5f), corner, 1.0f);

    // bottom lip highlight
    g.setColour (Colours::white.withAlpha (0.05f));
    g.drawHorizontalLine (roundToInt (area.getBottom()), area.getX() + corner, area.getRight() - corner);
}

void LookAndFeel::drawChannelFaderRuler (Graphics& g, Rectangle<float> area, ChannelFader& ts)
{
    static constexpr double marks[] { ChannelFader::maxDecibels, 3.0, 0.0, -3.0, -6.0, -10.0, -15.0,
                                      -20.0, -30.0, -40.0, -60.0, ChannelFader::minDecibels };

    const float right = area.getRight();

    // spine
    g.setColour (palette.textDim.withAlpha (0.5f));
    g.fillRect (right - 1.0f, area.getY(), 1.0f, area.getHeight());

    for (size_t i = 0; i < std::size (marks); ++i)
    {
        const float y = ts.getFaderY (marks[i]);

        if (exactlyEqual (marks[i], 0.0))
        {
            // unity: full width, accent, glowing
            const auto tick = Rectangle<float> (area.getX(), y - 1.0f, area.getWidth(), 2.0f);
            g.setColour (palette.accent.withAlpha (0.25f));
            g.fillRect (tick.expanded (0.0f, 2.0f));
            g.setColour (palette.accent);
            g.fillRect (tick);
        }
        else
        {
            g.setColour (palette.textDim);
            g.fillRect (right - area.getWidth() * 0.7f, y - 0.5f, area.getWidth() * 0.7f, 1.0f);
        }

        // minor tick halfway to the next mark
        if (i + 1 < std::size (marks))
        {
            const float midY = (y + ts.getFaderY (marks[i + 1])) * 0.5f;
            g.setColour (palette.textDim.withAlpha (0.55f));
            g.fillRect (right - area.getWidth() * 0.4f, midY - 0.5f, area.getWidth() * 0.4f, 1.0f);
        }
    }
}

void LookAndFeel::drawChannelFaderMeter (Graphics& g, Rectangle<float> scaleArea,
                                              Rectangle<float> meterArea, ChannelFader& ts)
{
    auto yFor = [&meterArea] (float dB)
    {
        return meterArea.getY() + ChannelFader::getMeterProportion (dB) * meterArea.getHeight();
    };

    //--------------------------------------------------------------------------
    // 1. dB scale: 0..30 in 3 dB steps, then 35..60 in 5 dB steps
    // The scale's labels shrink with a narrow (compact) strip so "60" still fits.
    g.setFont (makeFont (jlimit (7.5f, 10.0f, scaleArea.getWidth() * 0.5f)));
    for (int dB = 0; dB <= 60; dB += (dB < 30 ? 3 : 5))
    {
        const float y = yFor ((float) -dB);

        g.setColour (dB == 0 ? palette.text : palette.textDim.brighter (0.25f));
        g.drawText (String (dB), Rectangle<float> (scaleArea.getX(), y - 6.0f, scaleArea.getWidth() - 6.0f, 12.0f),
                    Justification::centredRight, false);

        g.setColour (palette.textDim);
        g.fillRect (scaleArea.getRight() - 4.0f, y - 0.5f, 3.0f, 1.0f);
    }

    //--------------------------------------------------------------------------
    // 2. Meter body (recessed)
    const auto body = meterArea.expanded (0.0f, 2.0f);
    g.setColour (palette.arcTrack);
    g.fillRoundedRectangle (body, 2.0f);
    g.setColour (palette.panelBorder);
    g.drawRoundedRectangle (body, 2.0f, 1.0f);

    // bottom (-60) -> top (0 dB): cool blue rising into the warm accent
    ColourGradient grad (palette.accentWarm,           0.0f, meterArea.getY(),
                         palette.accent.darker (0.5f), 0.0f, meterArea.getBottom(), false);
    grad.addColour (ChannelFader::getMeterProportion (-6.0f),  palette.accent.brighter (0.3f));
    grad.addColour (ChannelFader::getMeterProportion (-18.0f), palette.accent);

    const auto bar = meterArea.reduced (2.0f, 0.0f);

    // 3. Level fill with LED segmentation and a soft glow
    if (ts.getMeterLevel() > ChannelFader::meterMinDecibels)
    {
        const auto fill = bar.withTop (yFor (ts.getMeterLevel()));

        g.setGradientFill (grad);
        g.setOpacity (0.25f);
        g.fillRect (fill.expanded (2.0f, 0.0f));
        g.setOpacity (1.0f);
        g.fillRect (fill);

        g.setColour (palette.arcTrack.withAlpha (0.55f));
        for (float y = bar.getBottom() - 3.0f; y > fill.getY(); y -= 3.0f)
            g.fillRect (fill.getX(), y, fill.getWidth(), 1.0f);
    }

    // 4. Peak hold line
    if (ts.getMeterPeak() > ChannelFader::meterMinDecibels)
    {
        const float y = yFor (ts.getMeterPeak());
        const float proportion = (y - meterArea.getY()) / meterArea.getHeight();
        g.setColour (grad.getColourAtPosition (proportion).brighter (0.3f));
        g.fillRect (bar.getX(), y - 1.0f, bar.getWidth(), 2.0f);
    }
}

//==============================================================================
void LookAndFeel::drawButtonBackground (Graphics& g, juce::Button& button,
                                              const Colour& backgroundColour,
                                              bool highlighted, bool down)
{
    auto bounds = button.getLocalBounds().toFloat().reduced (3.0f);
    if (down)
        bounds.translate (0.0f, 1.0f);

    const float corner = jmin (8.0f, bounds.getHeight() * 0.3f);
    const bool  on     = button.getToggleState();

    Colour base = on ? button.findColour (TextButton::buttonOnColourId) : backgroundColour;
    if (! button.isEnabled())  base = base.withMultipliedSaturation (0.2f).withMultipliedAlpha (0.5f);
    else if (down)             base = base.darker (0.15f);
    else if (highlighted)      base = base.brighter (0.10f);

    // shadow (smaller when pressed)
    Path shape;
    shape.addRoundedRectangle (bounds, corner);
    DropShadow (Colours::black.withAlpha (down ? 0.35f : 0.55f),
                down ? 4 : 10, { 0, down ? 1 : 4 }).drawForPath (g, shape);

    // body
    g.setGradientFill (ColourGradient (base.brighter (0.12f), bounds.getX(), bounds.getY(),
                                       base.darker (0.25f),   bounds.getX(), bounds.getBottom(), false));
    g.fillPath (shape);

    // glow when toggled on
    if (on && button.isEnabled())
    {
        g.setColour (base.withAlpha (0.25f));
        g.drawRoundedRectangle (bounds.expanded (1.5f), corner + 1.5f, 3.0f);
    }

    // top highlight + dark outline
    g.setGradientFill (ColourGradient (Colours::white.withAlpha (0.18f), bounds.getX(), bounds.getY(),
                                       Colours::white.withAlpha (0.0f),  bounds.getX(), bounds.getCentreY(), false));
    g.drawRoundedRectangle (bounds.reduced (0.75f), corner, 1.0f);

    g.setColour (Colours::black.withAlpha (0.5f));
    g.drawRoundedRectangle (bounds, corner, 1.0f);
}

void LookAndFeel::drawButtonText (Graphics& g, TextButton& button,
                                        bool /*highlighted*/, bool down)
{
    auto bounds = button.getLocalBounds().reduced (6, 3);
    if (down)
        bounds.translate (0, 1);

    g.setFont (getTextButtonFont (button, button.getHeight()));
    g.setColour (button.findColour (button.getToggleState() ? TextButton::textColourOnId
                                                            : TextButton::textColourOffId)
                       .withMultipliedAlpha (button.isEnabled() ? 1.0f : 0.5f));
    g.drawFittedText (button.getButtonText().toUpperCase(), bounds, Justification::centred, 1, 0.8f);
}

Font LookAndFeel::getTextButtonFont (TextButton&, int buttonHeight)
{
    return makeFont (jlimit (11.0f, 18.0f, (float) buttonHeight * 0.36f), true)
               .withExtraKerningFactor (0.10f);
}

//==============================================================================
void LookAndFeel::drawBackground (Graphics& g, Rectangle<float> area)
{
    // vertical gradient
    g.setGradientFill (ColourGradient (palette.bgTop,    area.getX(), area.getY(),
                                       palette.bgBottom, area.getX(), area.getBottom(), false));
    g.fillRect (area);

    // soft light pool upper-centre
    g.setGradientFill (ColourGradient (palette.accent.withAlpha (0.06f), area.getCentreX(), area.getY() + area.getHeight() * 0.25f,
                                       Colours::transparentBlack,        area.getCentreX(), area.getBottom(), true));
    g.fillRect (area);

    // vignette
    g.setGradientFill (ColourGradient (Colours::transparentBlack,      area.getCentreX(), area.getCentreY(),
                                       Colours::black.withAlpha (0.45f), area.getX(),       area.getY(), true));
    g.fillRect (area);

    // grain
    g.setTiledImageFill (noiseTile, 0, 0, 1.0f);
    g.fillRect (area);
}

void LookAndFeel::drawPanel (Graphics& g, Rectangle<float> area, const String& title)
{
    const float corner = 12.0f;

    // recessed body
    g.setColour (palette.panelFill.withAlpha (0.85f));
    g.fillRoundedRectangle (area, corner);

    // inner shadow at top edge
    g.setGradientFill (ColourGradient (Colours::black.withAlpha (0.35f), area.getX(), area.getY(),
                                       Colours::transparentBlack,        area.getX(), area.getY() + 14.0f, false));
    g.fillRoundedRectangle (area, corner);

    // border + bottom lip highlight
    g.setColour (palette.panelBorder);
    g.drawRoundedRectangle (area.reduced (0.5f), corner, 1.0f);

    g.setGradientFill (ColourGradient (Colours::transparentWhite,       area.getX(), area.getCentreY(),
                                       Colours::white.withAlpha (0.05f), area.getX(), area.getBottom(), false));
    g.drawRoundedRectangle (area.reduced (1.5f), corner - 1.0f, 1.0f);

    if (title.isNotEmpty())
    {
        g.setFont (makeFont (12.0f, true).withExtraKerningFactor (0.18f));
        g.setColour (palette.textDim);
        g.drawText (title.toUpperCase(),
                    area.removeFromTop (panelTitleHeight).reduced (16.0f, 0.0f).toNearestInt(),
                    Justification::centredLeft, false);
    }
}

//==============================================================================
namespace
{
    struct SharedTheme final : private DeletedAtShutdown
    {
        ~SharedTheme() override { clearSingletonInstance(); }

        LookAndFeel lookAndFeel;

        JUCE_DECLARE_SINGLETON_SINGLETHREADED_MINIMAL_INLINE (SharedTheme)
    };
}

LookAndFeel& LookAndFeel::getShared()
{
    return SharedTheme::getInstance()->lookAndFeel;
}

LookAndFeel& getTheme (Component& component)
{
    if (auto* theme = dynamic_cast<LookAndFeel*> (&component.getLookAndFeel()))
        return *theme;

    return LookAndFeel::getShared();
}

//==============================================================================
namespace
{
    ui::Label::Style labelStyleOf (const juce::Label& label)
    {
        return (ui::Label::Style) (int) label.getProperties().getWithDefault (ui::Label::styleProperty,
                                                                              (int) ui::Label::Style::Body);
    }
}

Font LookAndFeel::getLabelFont (juce::Label& label)
{
    const auto height = (float) label.getHeight();

    switch (labelStyleOf (label))
    {
        case ui::Label::Style::Title:   return makeFont (jlimit (16.0f, 40.0f, height * 0.62f), true).withExtraKerningFactor (0.03f);
        case ui::Label::Style::Heading: return makeFont (jlimit (10.0f, 16.0f, height * 0.5f), true).withExtraKerningFactor (0.18f);
        case ui::Label::Style::Caption: return makeFont (jlimit (9.0f, 13.0f, height * 0.55f)).withExtraKerningFactor (0.04f);
        case ui::Label::Style::Value:   return makeFont (jlimit (11.0f, 18.0f, height * 0.55f), true).withExtraKerningFactor (0.04f);
        case ui::Label::Style::Body:    break;
    }

    return makeFont (jmax (11.0f, height * 0.6f)).withExtraKerningFactor (0.06f);
}

void LookAndFeel::drawLabel (Graphics& g, juce::Label& label)
{
    const auto style = labelStyleOf (label);
    const auto bounds = label.getLocalBounds().toFloat();

    if (style == ui::Label::Style::Value)
        drawReadout (g, bounds, label.isBeingEdited());
    else
        g.fillAll (label.findColour (juce::Label::backgroundColourId));

    if (label.isBeingEdited())
        return;

    const auto alpha = label.isEnabled() ? 1.0f : 0.5f;
    const auto text = style == ui::Label::Style::Heading ? label.getText().toUpperCase() : label.getText();
    const auto area = getLabelBorderSize (label).subtractedFrom (label.getLocalBounds());

    g.setFont (getLabelFont (label));
    g.setColour (label.findColour (juce::Label::textColourId).withMultipliedAlpha (alpha));
    g.drawFittedText (text, area, label.getJustificationType(),
                      jmax (1, (int) ((float) area.getHeight() / g.getCurrentFont().getHeight())),
                      label.getMinimumHorizontalScale());
}

//==============================================================================
void LookAndFeel::drawTableHeaderBackground (Graphics& g, TableHeaderComponent& header)
{
    const auto area = header.getLocalBounds().toFloat();

    g.setGradientFill (ColourGradient (palette.capBottom.brighter (0.05f), 0.0f, 0.0f,
                                       palette.panelFill,                 0.0f, area.getBottom(), false));
    g.fillRect (area);

    g.setColour (Colours::black.withAlpha (0.55f));
    g.fillRect (area.withTop (area.getBottom() - 1.0f));

    // column separators
    g.setColour (palette.panelBorder);

    for (int i = header.getNumColumns (true) - 1; --i >= 0;)
    {
        const auto x = (float) header.getColumnPosition (i).getRight();
        g.fillRect (x - 1.0f, area.getHeight() * 0.25f, 1.0f, area.getHeight() * 0.5f);
    }
}

void LookAndFeel::drawTableHeaderColumn (Graphics& g, TableHeaderComponent& header, const String& columnName,
                                         int /*columnId*/, int width, int height, bool isMouseOver,
                                         bool isMouseDown, int columnFlags)
{
    if (isMouseDown || isMouseOver)
    {
        g.setColour (Colours::white.withAlpha (isMouseDown ? 0.06f : 0.03f));
        g.fillRect (0, 0, width, height);
    }

    const auto justification = (columnFlags & TableHeaderComponent::sortedForwards) != 0
                                 || (columnFlags & TableHeaderComponent::sortedBackwards) != 0
                                 ? Justification::centredLeft : Justification::centred;

    g.setColour (header.findColour (TableHeaderComponent::textColourId));
    g.setFont (makeFont (jlimit (10.0f, 13.0f, (float) height * 0.45f), true).withExtraKerningFactor (0.1f));
    g.drawFittedText (columnName.toUpperCase(), Rectangle<int> (width, height).reduced (6, 0), justification, 1);
}

//==============================================================================
void LookAndFeel::drawDocumentWindowTitleBar (DocumentWindow& window, Graphics& g, int width, int height,
                                              int titleSpaceX, int titleSpaceW, const Image* icon,
                                              bool drawTitleTextOnLeft)
{
    if (width <= 0 || height <= 0)
        return;

    const auto area = Rectangle<int> (width, height).toFloat();

    g.setGradientFill (ColourGradient (palette.bgTop.brighter (0.08f), 0.0f, 0.0f,
                                       palette.panelFill,             0.0f, area.getBottom(), false));
    g.fillRect (area);

    // top highlight + bottom separator
    g.setColour (Colours::white.withAlpha (0.06f));
    g.fillRect (area.withHeight (1.0f));
    g.setColour (Colours::black.withAlpha (0.6f));
    g.fillRect (area.withTop (area.getBottom() - 1.0f));

    const auto font = makeFont (jlimit (11.0f, 16.0f, (float) height * 0.5f), true).withExtraKerningFactor (0.12f);
    g.setFont (font);

    auto textWidth = GlyphArrangement::getStringWidthInt (font, window.getName().toUpperCase());
    auto iconWidth = 0;

    if (icon != nullptr)
    {
        iconWidth = height - 8;
        textWidth += iconWidth + 6;
    }

    textWidth = jmin (titleSpaceW, textWidth);
    auto textX = drawTitleTextOnLeft ? titleSpaceX : jmax (titleSpaceX, (width - textWidth) / 2);

    if (icon != nullptr)
    {
        g.drawImageWithin (*icon, textX, 4, iconWidth, height - 8, RectanglePlacement::centred | RectanglePlacement::onlyReduceInSize);
        textX += iconWidth + 6;
        textWidth -= iconWidth + 6;
    }

    g.setColour (window.isActiveWindow() ? palette.text : palette.textDim);
    g.drawText (window.getName().toUpperCase(), textX, 0, textWidth, height, Justification::centredLeft, true);
}

} // namespace onebox::ui
