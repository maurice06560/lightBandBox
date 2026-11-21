namespace onebox::ui
{

namespace
{
    // Fader travel: gain (dB) -> normalised position (0 = bottom, 1 = top). Piecewise-linear, giving the
    // usual console feel: fine resolution around unity, the low end compressed towards -inf.
    constexpr double faderDb[]  { ChannelFader::minDecibels, -60.0, -40.0, -30.0, -20.0, -10.0, 0.0, ChannelFader::maxDecibels };
    constexpr double faderPos[] { 0.0,                        0.07,  0.17,  0.27,  0.40,  0.58,  0.80, 1.0 };
    constexpr int numBreakpoints = (int) std::size (faderDb);

    double interpolate (const double* xs, const double* ys, double x)
    {
        if (x <= xs[0])
            return ys[0];

        for (int i = 1; i < numBreakpoints; ++i)
            if (x <= xs[i])
                return ys[i - 1] + (ys[i] - ys[i - 1]) * (x - xs[i - 1]) / (xs[i] - xs[i - 1]);

        return ys[numBreakpoints - 1];
    }

    juce::NormalisableRange<double> makeFaderRange()
    {
        return { ChannelFader::minDecibels, ChannelFader::maxDecibels,
                 [] (double, double, double proportion) { return interpolate (faderPos, faderDb, proportion); },
                 [] (double, double, double dB)         { return interpolate (faderDb, faderPos, dB); },
                 [] (double start, double end, double dB)
                 {
                     return juce::jlimit (start, end, std::round (dB * 10.0) / 10.0);
                 } };
    }

    bool isAtBottom (double dB) { return dB <= ChannelFader::minDecibels + 0.05; }
}

//==============================================================================
ChannelFader::ChannelFader()
{
    fader.setSliderStyle (juce::Slider::LinearVertical);
    fader.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
    fader.setNormalisableRange (makeFaderRange());
    fader.setValue (0.0, juce::dontSendNotification);
    fader.setDoubleClickReturnValue (true, 0.0);
    fader.setSliderSnapsToMousePosition (false);   // grab the cap, don't jump to the click
    fader.setTextValueSuffix (" dB");
    fader.addListener (this);
    addAndMakeVisible (fader);

    gainReadout.setJustificationType (juce::Justification::centred);
    gainReadout.setEditable (false, true);
    gainReadout.setTooltip ("Double-click to type a gain value");
    gainReadout.onEditorShow = [this] { repaint(); };
    gainReadout.onEditorHide = [this] { repaint(); };
    gainReadout.onTextChange = [this]
    {
        const auto text = gainReadout.getText().trim();
        const bool isInf = text.containsIgnoreCase ("inf") || text.contains (juce::CharPointer_UTF8 ("\xe2\x88\x9e"));
        setGainDecibels (isInf ? minDecibels : juce::jlimit (minDecibels, maxDecibels, text.getDoubleValue()));
        updateReadouts();   // reformat even if the value did not change
    };
    addAndMakeVisible (gainReadout);

    peakReadout.setJustificationType (juce::Justification::centred);
    peakReadout.setInterceptsMouseClicks (false, false);   // clicks handled in mouseDown()
    addAndMakeVisible (peakReadout);

    updateReadouts();
}

ChannelFader::~ChannelFader()
{
    fader.removeListener (this);
}

//==============================================================================
void ChannelFader::setGainDecibels (double dB, juce::NotificationType notification)
{
    fader.setValue (dB, notification);
    updateReadouts();   // the listener only runs when notified, but the readout must always follow
}

float ChannelFader::getVolume() const
{
    const auto dB = fader.getValue();
    return isAtBottom (dB) ? -std::numeric_limits<float>::infinity() : (float) dB;
}

void ChannelFader::setVolume (float decibels, juce::NotificationType notification)
{
    setGainDecibels (std::isinf (decibels) && decibels < 0.0f ? minDecibels
                                                              : juce::jlimit (minDecibels, maxDecibels, (double) decibels),
                     notification);
}

void ChannelFader::setMeterLevel (float levelDb, float peakDb)
{
    meterLevelDb = juce::jlimit ((float) minDecibels, 0.0f, levelDb);
    meterPeakDb  = juce::jmax (meterLevelDb, juce::jlimit ((float) minDecibels, (float) maxDecibels, peakDb));
    updateReadouts();
    repaint();
}

void ChannelFader::resetPeak()
{
    meterPeakDb = meterLevelDb;
    updateReadouts();
    repaint();
}

float ChannelFader::getFaderY (double dB) const
{
    return (float) fader.getY() + (float) fader.getPositionOfValue (dB);
}

float ChannelFader::getMeterProportion (float dB)
{
    // Equal spacing per label: 3 dB steps down to -30, then 5 dB steps to -60.
    const float down = juce::jlimit (0.0f, -meterMinDecibels, -dB);
    const float step = down <= 30.0f ? down / 3.0f
                                     : 10.0f + (down - 30.0f) / 5.0f;
    return step / 16.0f;
}

juce::String ChannelFader::decibelsToText (double dB)
{
    if (isAtBottom (dB))
        return juce::String (juce::CharPointer_UTF8 ("-\xe2\x88\x9e"));

    if (std::abs (dB) < 0.05)
        return "0.0";

    return (dB > 0.0 ? "+" : "") + juce::String (dB, 1);
}

//==============================================================================
void ChannelFader::paint (juce::Graphics& g)
{
    // Outside a themed hierarchy, fall back to the shared OneBox theme so the strip still draws.
    auto* lf = dynamic_cast<LookAndFeelMethods*> (&getLookAndFeel());

    if (lf == nullptr)
        lf = &LookAndFeel::getShared();

    lf->drawChannelFaderBackground (g, getLocalBounds().toFloat(), *this);
    lf->drawChannelFaderReadout (g, gainReadout.getBounds().toFloat(), gainReadout.isBeingEdited(), *this);
    lf->drawChannelFaderReadout (g, peakReadout.getBounds().toFloat(), false, *this);
    lf->drawChannelFaderRuler (g, rulerArea, *this);
    lf->drawChannelFaderMeter (g, scaleArea, meterArea, *this);
}

void ChannelFader::resized()
{
    // Below compactWidth the strip switches to a narrow, Logic-style layout: tighter margins,
    // a thinner ruler, scale and meter, so the fader keeps a usable width.
    const auto compact = getWidth() < compactWidth;

    auto area = getLocalBounds().reduced (compact ? 3 : 10, compact ? 6 : 10);

    auto readouts = area.removeFromTop (compact ? 18 : 24);
    const auto readoutGap = compact ? 2 : 6;
    gainReadout.setBounds (readouts.removeFromLeft ((readouts.getWidth() - readoutGap) / 2));
    readouts.removeFromLeft (readoutGap);
    peakReadout.setBounds (readouts);

    area.removeFromTop (compact ? 6 : 10);

    auto meter = area.removeFromRight (compact ? 5 : 10);
    area.removeFromRight (compact ? 2 : 3);
    auto scale = area.removeFromRight (compact ? 15 : 24);
    area.removeFromRight (compact ? 1 : 2);
    auto ruler = area.removeFromLeft (compact ? 7 : 14);
    fader.setBounds (area);
    Fader::matchDragToTravel (fader);

    // Ruler, scale and meter all span the fader cap's travel so marks line up.
    const float travelTop    = getFaderY (maxDecibels);
    const float travelBottom = getFaderY (minDecibels);
    rulerArea = ruler.toFloat().withTop (travelTop).withBottom (travelBottom);
    scaleArea = scale.toFloat().withTop (travelTop).withBottom (travelBottom);
    meterArea = meter.toFloat().withTop (travelTop).withBottom (travelBottom);
}

void ChannelFader::mouseDown (const juce::MouseEvent& e)
{
    if (peakReadout.getBounds().contains (e.getPosition()))
        resetPeak();
}

void ChannelFader::lookAndFeelChanged()
{
    const auto text = findColour (juce::Label::textColourId);

    for (auto* label : { &gainReadout, &peakReadout })
    {
        label->setColour (juce::Label::backgroundColourId,            juce::Colours::transparentBlack);
        label->setColour (juce::Label::outlineColourId,               juce::Colours::transparentBlack);
        label->setColour (juce::Label::backgroundWhenEditingColourId, juce::Colours::transparentBlack);
        label->setColour (juce::Label::outlineWhenEditingColourId,    juce::Colours::transparentBlack);
        label->setColour (juce::Label::textWhenEditingColourId,       text.brighter (0.4f));
    }

    repaint();
}

//==============================================================================
void ChannelFader::sliderValueChanged (juce::Slider*)
{
    updateReadouts();

    if (onVolumeChange != nullptr)
        onVolumeChange (getVolume());
}

void ChannelFader::updateReadouts()
{
    gainReadout.setText (decibelsToText (fader.getValue()), juce::dontSendNotification);
    peakReadout.setText (meterPeakDb > minDecibels ? decibelsToText (meterPeakDb) : juce::String(),
                         juce::dontSendNotification);
}

} // namespace onebox::ui
