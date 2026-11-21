#include "TunerPanel.h"
#include "../Audio/MBoxAudioEngine.h"

namespace
{
    constexpr double inTuneCents = 3.0;      // the needle turns green within this
    constexpr double holdMs = 600.0;         // a reading stays this long after the pitch is lost
    const juce::Colour inTuneColour { 0xff5fd39a };
}

TunerPanel::TunerPanel (Session& session)
    : m_session (session)
{
    for (auto* c : std::initializer_list<juce::Component*> { &m_inputCaption, &m_inputMenu, &m_footer })
        addAndMakeVisible (c);

    m_inputMenu.setTooltip ("The input to tune: while the tuner is open it is heard nowhere else");
    m_inputMenu.onChange = [this] { inputChosen(); };
    m_footer.setJustificationType (juce::Justification::centred);
    refreshInputMenu();
    updateFooter();
    setSize (460, 340);
}

TunerPanel::~TunerPanel()
{
    stop();
}

void TunerPanel::start()
{
    if (m_input < 0)
    {
        // First time: the selected track's input when it is a physical one.
        const auto* track = m_session.getSelectedTrack();
        m_input = track != nullptr && track->getTrackType() == TrackType::Physical ? juce::jmax (1, track->getPhysicalInput()) - 1 : 0;
    }

    if (const auto result = m_session.getApp().initialiseAudio(); result.failed())
    {
        m_session.setStatus ("Tuner: " + result.getErrorMessage(), true);
        return;
    }

    m_running = true;
    refreshInputMenu();
    applyInput();
    startTimerHz (30);
}

void TunerPanel::stop()
{
    stopTimer();

    if (! m_running)
        return;

    m_running = false;

    if (auto* engine = m_session.getApp().getAudioEngine())
        engine->stopTuner();

    showReading (std::nullopt);
    m_session.setStatus ("Tuner closed: input " + juce::String (m_input + 1) + " is back on its outputs");
}

void TunerPanel::applyInput()
{
    auto* engine = m_session.getApp().getAudioEngine();

    if (engine == nullptr)
        return;

    if (m_input >= engine->getInputChannelNames().size())
        m_input = 0;

    if (const auto result = engine->startTuner (m_input); result.failed())
    {
        m_session.setStatus ("Tuner: " + result.getErrorMessage(), true);
        return;
    }

    m_detector.prepare (engine->getCurrentSampleRate());
    m_recent.clear();
    showReading (std::nullopt);
    m_inputMenu.setSelectedId (m_input + 1, juce::dontSendNotification);
    updateFooter();
    m_session.setStatus ("Tuner on input " + juce::String (m_input + 1) + ": it is muted on every output until the tuner closes");
}

void TunerPanel::refreshInputMenu()
{
    // The device's inputs once it is open; until then a generic list (the device opens with start()).
    const auto* engine = std::as_const (m_session.getApp()).getAudioEngine();
    const auto open = engine != nullptr && engine->isInitialised();
    const auto names = open ? engine->getInputChannelNames() : juce::StringArray();
    const auto count = names.isEmpty() ? 8 : names.size();

    m_inputMenu.clear (juce::dontSendNotification);

    for (int channel = 0; channel < count; ++channel)
        m_inputMenu.addItem ("Input " + juce::String (channel + 1) + (channel < names.size() ? "   " + names[channel] : juce::String()), channel + 1);

    m_inputMenu.setSelectedId (juce::jmax (0, m_input) + 1, juce::dontSendNotification);
    m_menuGeneration = open ? engine->getDeviceGeneration() : -1;
}

void TunerPanel::inputChosen()
{
    m_input = m_inputMenu.getSelectedId() - 1;

    if (m_running)
        applyInput();
    else
        updateFooter();
}

void TunerPanel::updateFooter()
{
    m_footer.setText ("Input " + juce::String (juce::jmax (0, m_input) + 1) + " is muted on every output while the tuner is open",
                      juce::dontSendNotification);
}

void TunerPanel::timerCallback()
{
    auto* engine = m_session.getApp().getAudioEngine();

    if (engine == nullptr)
        return;

    // The device restarted (maybe another one, or another rate): rebuild the menu, take the input again.
    if (engine->getDeviceGeneration() != m_menuGeneration)
    {
        refreshInputMenu();
        applyInput();
        return;
    }

    engine->drainTuner ([this] (const float* samples, int count) { m_detector.push (samples, count); });

    const auto now = juce::Time::getMillisecondCounterHiRes();

    if (const auto detected = m_detector.detect())
    {
        // A median of the last few readings drops the odd octave slip at a note's attack.
        m_recent.push_back (*detected);

        if (m_recent.size() > 5)
            m_recent.pop_front();

        auto sorted = std::vector<double> (m_recent.begin(), m_recent.end());
        std::nth_element (sorted.begin(), sorted.begin() + (std::ptrdiff_t) (sorted.size() / 2), sorted.end());
        const auto median = sorted[sorted.size() / 2];

        const auto previous = m_frequency;
        m_frequency = median;
        m_lastDetection = now;

        const auto note = PitchDetector::noteFor (median);

        // Glide the needle within a note; jump to a new one.
        if (previous.has_value() && PitchDetector::noteFor (*previous).midi == note.midi)
            m_shownCents += 0.35 * (note.cents - m_shownCents);
        else
            m_shownCents = note.cents;

        repaint (m_display);
    }
    else if (m_frequency.has_value() && now - m_lastDetection > holdMs)
    {
        m_recent.clear();
        showReading (std::nullopt);
    }
}

void TunerPanel::showReading (std::optional<double> frequency)
{
    m_frequency = frequency;
    m_shownCents = frequency.has_value() ? PitchDetector::noteFor (*frequency).cents : 0.0;
    m_lastDetection = juce::Time::getMillisecondCounterHiRes();
    repaint();
}

//==============================================================================
void TunerPanel::resized()
{
    auto area = getLocalBounds().reduced (16);

    auto top = area.removeFromTop (28);
    m_inputCaption.setBounds (top.removeFromLeft (44));
    m_inputMenu.setBounds (top);

    area.removeFromTop (12);
    m_footer.setBounds (area.removeFromBottom (18));
    area.removeFromBottom (8);
    m_display = area;
}

void TunerPanel::paint (juce::Graphics& g)
{
    const auto& palette = ui::getTheme (*this).getPalette();
    const auto box = m_display.toFloat();

    g.setColour (palette.arcTrack);
    g.fillRoundedRectangle (box, 6.0f);
    g.setColour (palette.panelBorder);
    g.drawRoundedRectangle (box.reduced (0.5f), 6.0f, 1.0f);

    auto area = box.reduced (18.0f, 12.0f);
    const auto has = m_frequency.has_value();
    const auto note = has ? PitchDetector::noteFor (*m_frequency) : PitchDetector::Note {};
    const auto cents = juce::jlimit (-50.0, 50.0, m_shownCents);
    const auto inTune = has && std::abs (cents) <= inTuneCents;
    const auto needleColour = ! has ? palette.textDim : inTune ? inTuneColour : std::abs (cents) > 15.0 ? palette.accentWarm : palette.solo;

    // The note, its octave as a subscript.
    auto noteArea = area.removeFromTop (area.getHeight() * 0.52f);
    {
        const auto name = has ? note.name : juce::String (juce::CharPointer_UTF8 ("\xe2\x80\x93"));
        const auto bigFont = ui::LookAndFeel::makeFont (noteArea.getHeight() * 0.8f, true);
        const auto smallFont = ui::LookAndFeel::makeFont (noteArea.getHeight() * 0.3f, true);
        const auto octave = has ? juce::String (note.octave) : juce::String();
        const auto nameWidth = juce::GlyphArrangement::getStringWidth (bigFont, name);
        const auto octaveWidth = octave.isEmpty() ? 0.0f : juce::GlyphArrangement::getStringWidth (smallFont, octave) + 4.0f;
        auto line = noteArea.withSizeKeepingCentre (nameWidth + octaveWidth, noteArea.getHeight());

        g.setColour (has ? (inTune ? inTuneColour : palette.text.brighter (0.3f)) : palette.textDim);
        g.setFont (bigFont);
        g.drawText (name, line.removeFromLeft (nameWidth), juce::Justification::centred, false);

        if (octave.isNotEmpty())
        {
            g.setFont (smallFont);
            g.drawText (octave, line.withTrimmedLeft (4.0f).withTrimmedTop (line.getHeight() * 0.45f), juce::Justification::topLeft, false);
        }
    }

    // The cents scale: ticks every 10, labels every 25, the in-tune zone, then the needle.
    auto readout = area.removeFromBottom (20.0f);
    area.removeFromBottom (4.0f);
    auto scale = area.reduced (12.0f, 0.0f);
    const auto labels = scale.removeFromBottom (14.0f);
    const auto mid = scale.getCentreX();
    auto xFor = [&] (double c) { return mid + (float) (c / 50.0) * scale.getWidth() * 0.5f; };

    g.setColour (inTuneColour.withAlpha (inTune ? 0.35f : 0.15f));
    g.fillRoundedRectangle (juce::Rectangle<float> (xFor (-inTuneCents), scale.getY(), xFor (inTuneCents) - xFor (-inTuneCents), scale.getHeight()), 2.0f);

    g.setFont (ui::LookAndFeel::makeFont (10.0f));

    for (int c = -50; c <= 50; c += 10)
    {
        const auto x = xFor (c);
        const auto major = c % 50 == 0 || c == 0;
        const auto height = c == 0 ? scale.getHeight() : (major ? scale.getHeight() * 0.6f : scale.getHeight() * 0.35f);
        g.setColour (c == 0 ? palette.text : palette.textDim);
        g.fillRect (x - 0.5f, scale.getBottom() - height, 1.0f, height);
    }

    for (const auto c : { -50, -25, 0, 25, 50 })
    {
        g.setColour (palette.textDim.brighter (0.25f));
        g.drawText (c > 0 ? "+" + juce::String (c) : juce::String (c), juce::Rectangle<float> (xFor (c) - 20.0f, labels.getY() + 2.0f, 40.0f, 12.0f),
                    juce::Justification::centred, false);
    }

    g.setFont (ui::LookAndFeel::makeFont (16.0f, true));
    g.setColour (palette.textDim);
    g.drawText (juce::CharPointer_UTF8 ("\xe2\x99\xad"), juce::Rectangle<float> (box.getX() + 6.0f, scale.getY(), 20.0f, scale.getHeight()), juce::Justification::centred, false);
    g.drawText (juce::CharPointer_UTF8 ("\xe2\x99\xaf"), juce::Rectangle<float> (box.getRight() - 26.0f, scale.getY(), 20.0f, scale.getHeight()), juce::Justification::centred, false);

    if (has)
    {
        const auto x = xFor (cents);
        g.setColour (needleColour);
        g.fillRoundedRectangle (x - 2.0f, scale.getY() - 2.0f, 4.0f, scale.getHeight() + 4.0f, 2.0f);

        juce::Path arrow;
        arrow.addTriangle (x - 6.0f, scale.getY() - 8.0f, x + 6.0f, scale.getY() - 8.0f, x, scale.getY() - 1.0f);
        g.fillPath (arrow);
    }

    // Frequency and deviation.
    g.setFont (ui::LookAndFeel::makeFont (13.0f, true));
    g.setColour (has ? palette.text : palette.textDim);

    if (has)
    {
        const auto shown = juce::roundToInt (cents);
        const auto deviation = shown == 0 ? juce::String ("in tune") : (shown > 0 ? "+" : "") + juce::String (shown) + (std::abs (shown) == 1 ? " cent" : " cents");
        g.drawText (juce::String (*m_frequency, 1) + " Hz     " + deviation, readout, juce::Justification::centred, false);
    }
    else
    {
        g.drawText ("Play a string", readout, juce::Justification::centred, false);
    }

    g.setFont (ui::LookAndFeel::makeFont (10.0f));
    g.setColour (palette.textDim);
    g.drawText ("A4 = 440 Hz", box.reduced (10.0f, 6.0f), juce::Justification::topRight, false);
}
