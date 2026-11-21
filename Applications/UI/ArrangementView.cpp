#include "ArrangementView.h"

namespace
{
    constexpr double extraBarsPastEnd = 1.0;   // a little room after END, even fully zoomed out

    /** A small "input jack" glyph for Physical tracks. */
    juce::Path makeInputIcon()
    {
        juce::Path p;
        p.addEllipse (0.25f, 0.0f, 0.5f, 0.5f);
        p.addRoundedRectangle (0.42f, 0.45f, 0.16f, 0.45f, 0.05f);
        p.addRoundedRectangle (0.2f, 0.88f, 0.6f, 0.12f, 0.05f);
        return p;
    }

    /** Three dots for Info (cue click) tracks. */
    juce::Path makeInfoIcon()
    {
        juce::Path p;

        for (int i = 0; i < 3; ++i)
            p.addEllipse ((float) i * 0.36f, 0.36f, 0.28f, 0.28f);

        return p;
    }

    /** Waveform bars for WAV tracks. */
    juce::Path makeWaveIcon()
    {
        juce::Path p;
        const float heights[] { 0.3f, 0.6f, 1.0f, 0.7f, 0.45f, 0.8f, 0.35f };

        for (int i = 0; i < 7; ++i)
            p.addRoundedRectangle ((float) i / 7.0f, 0.5f - heights[i] * 0.5f, 0.09f, heights[i], 0.04f);

        return p;
    }
}

ArrangementView::ArrangementView (Session& session)
    : m_session (session)
{
    m_formats.registerBasicFormats();

    for (auto* c : std::initializer_list<juce::Component*> { &m_horizontal, &m_vertical, &m_addTrack, &m_removeTrack, &m_zoomCaption, &m_zoom })
        addAndMakeVisible (c);

    m_horizontal.addListener (this);
    m_vertical.addListener (this);
    m_horizontal.setAutoHide (false);
    m_vertical.setAutoHide (true);

    m_addTrack.setTooltip ("Add a track");
    m_removeTrack.setTooltip ("Remove the selected track");
    m_addTrack.onClick = [this] { addTrack(); };
    m_removeTrack.onClick = [this] { removeSelectedTrack(); };

    m_zoom.setRange (0.0, 1.0);
    // The cap sits under the pointer at all times (a click jumps it there). A drag that overshoots
    // either end picks the pointer up again as soon as it's back inside the travel, without the
    // dead zone a grab offset leaves.
    m_zoom.setSliderSnapsToMousePosition (true);
    m_zoom.setTooltip ("Left: the whole song. Right: one bar.");
    m_zoom.onValueChange = [this] { setZoomProportion (m_zoom.getValue(), cursorX()); };

    m_session.addListener (this);
    sessionChanged();
    startTimerHz (30);
}

ArrangementView::~ArrangementView()
{
    m_session.removeListener (this);

    for (auto& [path, thumbnail] : m_thumbnails)
        thumbnail->removeChangeListener (this);
}

//==============================================================================
void ArrangementView::sessionChanged()
{
    auto* song = m_session.getSelectedSong();
    m_tempoMap = song != nullptr ? std::make_unique<TempoMap> (*song) : nullptr;
    const auto unlocked = ! m_session.isEditLocked();
    m_removeTrack.setEnabled (song != nullptr && ! song->getTracks().empty() && unlocked);
    m_addTrack.setEnabled (song != nullptr && unlocked);

    // A newly selected song opens zoomed out on the whole song; otherwise keep the zoom proportion,
    // since the song length (and so the zoom range) may have changed.
    const auto songKey = m_session.getSelectedPlaylistName() + "/" + m_session.getSelectedSongName();

    if (songKey != m_zoomedSong)
    {
        m_zoomedSong = songKey;
        m_scrollBeats = 0.0;
        setZoomProportion (0.0, (float) headerWidth);
    }
    else
    {
        setZoomProportion (m_zoom.getValue(), cursorX());
    }

    updateScrollBars();
    repaint();
}

void ArrangementView::timerCallback()
{
    if (followPlayhead())
    {
        m_lastActiveRegions = activeRegions();
        m_lastPlayheadX = playheadX();
        repaint();
        return;
    }

    const auto x = playheadX();

    // When the highlighted bankslot regions change (play starts, the count-in ends, the playhead crosses
    // a switch, playback stops), whole regions change colour: repaint everything once. Repainting only
    // the playhead strip would leave a band of the new colour inside the old one.
    if (auto regions = activeRegions(); regions != m_lastActiveRegions)
    {
        m_lastActiveRegions = std::move (regions);
        m_lastPlayheadX = x;
        repaint();
        return;
    }

    if (std::abs (x - m_lastPlayheadX) < 0.5f)
        return;

    // Otherwise only the playhead moves: repaint just the strips under its old and new positions.
    for (auto px : { m_lastPlayheadX, x })
        if (px >= 0.0f)
            repaint (juce::Rectangle<int> ((int) px - 8, 0, 16, getHeight()));

    m_lastPlayheadX = x;
}

bool ArrangementView::followPlayhead()
{
    const auto middle = (float) timelineArea().getCentreX();

    if (m_tempoMap == nullptr || ! m_session.isPlaying())
    {
        // Paused, the playhead resumes where it is; stopped, play starts from the left locator.
        const auto x = m_session.getPlayheadFrames().has_value() ? playheadX() : cursorX();
        m_follow.park (x - middle);
        return false;
    }

    const auto gap = playheadX() - middle;
    const auto halfWidth = (float) timelineArea().getWidth() * 0.5f;

    // Off screen after a scroll or zoom: recentre, but not in the middle of the gesture.
    constexpr double gestureGraceMs = 500.0;

    if (std::abs (gap) > halfWidth && juce::Time::getMillisecondCounterHiRes() - m_lastViewGestureMs < gestureGraceMs)
        return false;

    const auto scroll = m_follow.step (gap, halfWidth);

    if (juce::exactlyEqual (scroll, 0.0f))
        return false;

    const auto before = m_scrollBeats;
    m_scrollBeats += (double) scroll / m_pixelsPerBeat;
    updateScrollBars();   // clamps at the end of the song, where the playhead then runs on to END

    return std::abs (m_scrollBeats - before) * m_pixelsPerBeat >= 0.01;
}

void ArrangementView::changeListenerCallback (juce::ChangeBroadcaster*)
{
    m_waveTiles.clear();   // a waveform (more of it) finished loading
    repaint();
}

void ArrangementView::scrollBarMoved (juce::ScrollBar* bar, double newRangeStart)
{
    if (bar == &m_horizontal)
    {
        m_scrollBeats = newRangeStart;
        m_follow.reset();   // glide back to the playhead, don't snap
        m_lastViewGestureMs = juce::Time::getMillisecondCounterHiRes();
    }
    else
        m_scrollY = (int) newRangeStart;

    repaint();
}

//==============================================================================
juce::Rectangle<int> ArrangementView::timelineArea() const
{
    return getLocalBounds().withTrimmedLeft (headerWidth).withTrimmedBottom (scrollBarSize).withTrimmedRight (scrollBarSize);
}

float ArrangementView::xForBeats (double beats) const
{
    return (float) headerWidth + (float) ((beats - m_scrollBeats) * m_pixelsPerBeat);
}

double ArrangementView::beatsForX (float x) const
{
    return m_scrollBeats + (double) (x - (float) headerWidth) / m_pixelsPerBeat;
}

double ArrangementView::visibleBeats() const
{
    return (double) timelineArea().getWidth() / m_pixelsPerBeat;
}

double ArrangementView::totalBeats() const
{
    if (m_tempoMap == nullptr)
        return visibleBeats();

    const auto lastBar = m_tempoMap->barLengthInBeats (m_tempoMap->getTotalBars());
    return juce::jmax (visibleBeats(), m_tempoMap->getLengthInBeats() + extraBarsPastEnd * lastBar);
}

juce::Rectangle<int> ArrangementView::trackRowBounds (int index) const
{
    return { 0, tracksTop() + index * trackHeight - m_scrollY, getWidth() - scrollBarSize, trackHeight };
}

void ArrangementView::updateScrollBars()
{
    m_horizontal.setRangeLimits (0.0, totalBeats(), juce::dontSendNotification);
    m_horizontal.setCurrentRange (m_scrollBeats, visibleBeats(), juce::dontSendNotification);
    m_scrollBeats = m_horizontal.getCurrentRangeStart();

    auto* song = m_session.getSelectedSong();
    const auto tracksHeight = song != nullptr ? (int) song->getTracks().size() * trackHeight : 0;

    const auto visibleHeight = getHeight() - tracksTop() - scrollBarSize;

    m_vertical.setRangeLimits (0.0, juce::jmax (tracksHeight, visibleHeight), juce::dontSendNotification);
    m_vertical.setCurrentRange (m_scrollY, visibleHeight, juce::dontSendNotification);
    m_scrollY = (int) m_vertical.getCurrentRangeStart();
}

double ArrangementView::minZoom() const
{
    auto* song = const_cast<Session&> (m_session).getSelectedSong();
    const auto width = (double) juce::jmax (1, timelineArea().getWidth());

    if (song == nullptr || m_tempoMap == nullptr)
        return width / 64.0;

    const auto lastBar = m_tempoMap->barLengthInBeats (m_tempoMap->getTotalBars());
    return width / (m_tempoMap->getLengthInBeats() + extraBarsPastEnd * lastBar);
}

double ArrangementView::maxZoom() const
{
    auto* song = const_cast<Session&> (m_session).getSelectedSong();
    double longestBar = 4.0;

    if (song != nullptr)
        for (const auto& [bar, signature] : song->getSignatureGrid())
            longestBar = juce::jmax (longestBar, (double) signature.ticksPerBar() / ticksPerQuarterNote);

    return juce::jmax (minZoom(), (double) juce::jmax (1, timelineArea().getWidth()) / longestBar);
}

double ArrangementView::zoomProportion() const
{
    const auto lo = minZoom(), hi = maxZoom();
    return hi <= lo ? 0.0 : juce::jlimit (0.0, 1.0, std::log (m_pixelsPerBeat / lo) / std::log (hi / lo));
}

void ArrangementView::setZoomProportion (double proportion, float anchorX)
{
    const auto lo = minZoom(), hi = maxZoom();
    setZoom (hi <= lo ? lo : lo * std::pow (hi / lo, juce::jlimit (0.0, 1.0, proportion)), anchorX);
}

void ArrangementView::setZoom (double pixelsPerBeat, float anchorX)
{
    const auto anchorBeats = beatsForX (anchorX);
    const auto previous = std::exchange (m_pixelsPerBeat, juce::jlimit (minZoom(), maxZoom(), pixelsPerBeat));

    // A real zoom moves the playhead off the middle: glide back to it. (Panels refresh through here
    // too, e.g. on every live fader move, and those must leave a followed playhead alone.)
    if (std::abs (previous - m_pixelsPerBeat) > 1.0e-6 * m_pixelsPerBeat)
    {
        m_follow.reset();
        m_lastViewGestureMs = juce::Time::getMillisecondCounterHiRes();
    }

    m_scrollBeats = juce::jmax (0.0, anchorBeats - (double) (anchorX - (float) headerWidth) / m_pixelsPerBeat);
    m_zoom.setValue (zoomProportion(), juce::dontSendNotification);
    updateScrollBars();
    repaint();
}

ArrangementView::Hit ArrangementView::hitAt (juce::Point<float> p) const
{
    Hit hit;
    hit.beats = beatsForX (p.x);

    if (p.y < (float) rulerHeight)
        hit.zone = p.x >= (float) headerWidth ? Zone::Ruler : Zone::None;
    else if (p.y < (float) (rulerHeight + tempoLaneHeight))
        hit.zone = p.x >= (float) headerWidth ? Zone::TempoLane : Zone::None;
    else if (p.y < (float) tracksTop())
        hit.zone = p.x >= (float) headerWidth ? Zone::SignatureLane : Zone::None;
    else
    {
        auto* song = const_cast<Session&> (m_session).getSelectedSong();
        const auto index = (int) ((p.y - (float) tracksTop() + (float) m_scrollY) / (float) trackHeight);

        if (song != nullptr && index >= 0 && index < (int) song->getTracks().size())
        {
            hit.trackIndex = index;
            hit.zone = p.x < (float) headerWidth ? Zone::TrackHeader : Zone::TrackLane;
        }
    }

    return hit;
}

Bar ArrangementView::barAt (double beats, bool snapToBeat) const
{
    if (m_tempoMap == nullptr)
        return {};

    const auto bar = m_tempoMap->beatsToBar (juce::jmax (0.0, beats));
    const auto lastBar = m_tempoMap->getTotalBars();

    if (bar.m_bar > lastBar)
        return Bar { lastBar, 1, 1, 0 };

    return snapToBeat ? Bar { bar.m_bar, bar.m_beat, 1, 0 } : Bar { bar.m_bar, 1, 1, 0 };
}

double ArrangementView::activePlayBeats() const
{
    auto& session = const_cast<Session&> (m_session);
    const auto frames = session.getPlayheadFrames();
    auto* song = session.getSelectedSong();

    // No highlight before the left locator: that's the count-in
    if (m_tempoMap == nullptr || song == nullptr || ! frames.has_value()
        || *frames < m_tempoMap->getTimeline().barToFrames (song->getLeftLocator()))
        return -1.0;

    return m_tempoMap->framesToBeats (*frames);
}

std::vector<int> ArrangementView::activeRegions() const
{
    std::vector<int> regions;
    auto* song = const_cast<Session&> (m_session).getSelectedSong();
    const auto playBeats = activePlayBeats();

    if (song == nullptr || playBeats < 0.0)
        return regions;

    // Same rule as paintBankRegions: a region runs from its switch to the next one (or the song end).
    for (const auto& track : song->getTracks())
    {
        int active = -1;

        if (const auto* physical = dynamic_cast<const MBoxPhysicalTrack*> (track.get()))
        {
            const auto& grid = physical->getBankSlotChanges();

            for (size_t i = 0; i < grid.size(); ++i)
                if (playBeats >= m_tempoMap->toBeats (grid[i].first))
                    active = (int) i;
        }

        regions.push_back (active);
    }

    return regions;
}

float ArrangementView::cursorX() const
{
    if (m_tempoMap == nullptr)
        return (float) headerWidth;

    // Works off-screen too: the cursor keeps its (virtual) x, and the view zooms around it.
    return xForBeats (m_tempoMap->toBeats (const_cast<Session&> (m_session).getCursor()));
}

float ArrangementView::playheadX() const
{
    const auto frames = m_session.getPlayheadFrames();

    if (m_tempoMap == nullptr || ! frames.has_value())
        return -1.0f;

    return xForBeats (m_tempoMap->framesToBeats (*frames));
}

//==============================================================================
void ArrangementView::resized()
{
    auto corner = juce::Rectangle<int> (0, 0, headerWidth, rulerHeight).reduced (8, 6);
    m_addTrack.setBounds (corner.removeFromLeft (34));
    corner.removeFromLeft (4);
    m_removeTrack.setBounds (corner.removeFromLeft (34));
    corner.removeFromLeft (10);
    m_zoomCaption.setBounds (corner.removeFromLeft (36));
    m_zoom.setBounds (corner);

    m_horizontal.setBounds (headerWidth, getHeight() - scrollBarSize, getWidth() - headerWidth - scrollBarSize, scrollBarSize);
    setZoomProportion (m_zoom.getValue(), cursorX());
    m_vertical.setBounds (getWidth() - scrollBarSize, tracksTop(), scrollBarSize, getHeight() - tracksTop() - scrollBarSize);
    updateScrollBars();
}

void ArrangementView::paint (juce::Graphics& g)
{
    const auto& palette = ui::getTheme (*this).getPalette();
    g.fillAll (palette.bgBottom.darker (0.15f));

    if (m_session.getSelectedSong() == nullptr || m_tempoMap == nullptr)
    {
        g.setColour (palette.textDim);
        g.setFont (ui::LookAndFeel::makeFont (15.0f));
        g.drawFittedText ("Select a song in the playlist browser, or create one with \"+ Song\".",
                          getLocalBounds().reduced (40), juce::Justification::centred, 2);
        return;
    }

    // Header column background
    g.setGradientFill (juce::ColourGradient (palette.panelFill.brighter (0.04f), 0.0f, 0.0f,
                                             palette.panelFill, (float) headerWidth, 0.0f, false));
    g.fillRect (0, 0, headerWidth, getHeight());

    paintTracks (g);

    // The global lanes end where the track lanes do, left of the vertical scrollbar.
    const auto laneWidth = timelineArea().getWidth();
    paintRuler (g, { headerWidth, 0, laneWidth, rulerHeight });
    paintTempoLane (g, { 0, rulerHeight, headerWidth, tempoLaneHeight },
                    { headerWidth, rulerHeight, laneWidth, tempoLaneHeight });
    paintSignatureLane (g, { 0, rulerHeight + tempoLaneHeight, headerWidth, signatureLaneHeight },
                        { headerWidth, rulerHeight + tempoLaneHeight, laneWidth, signatureLaneHeight });

    // Corner above the headers
    g.setColour (palette.panelFill.brighter (0.02f));
    g.fillRect (0, 0, headerWidth, rulerHeight);

    // Separators: header column edge, below the global lanes
    g.setColour (juce::Colours::black.withAlpha (0.7f));
    g.fillRect (headerWidth - 1, 0, 1, getHeight());
    g.fillRect (0, tracksTop() - 1, getWidth(), 2);
    g.setColour (palette.panelBorder);
    g.fillRect (0, rulerHeight - 1, getWidth(), 1);
    g.fillRect (0, rulerHeight + tempoLaneHeight - 1, getWidth(), 1);

    paintPlayheadAndCursor (g);
}

void ArrangementView::paintGrid (juce::Graphics& g, juce::Rectangle<int> area, bool strong)
{
    const auto totalBars = m_tempoMap->getTotalBars();
    const auto lastBar = totalBars + (int) extraBarsPastEnd;
    const auto& palette = ui::getTheme (*this).getPalette();

    for (int bar = 1; bar <= lastBar + 1; ++bar)
    {
        const auto beats = bar <= totalBars + 1
                               ? m_tempoMap->toBeats (Bar { bar, 1, 1, 0 })
                               : m_tempoMap->getLengthInBeats() + (bar - totalBars - 1) * m_tempoMap->barLengthInBeats (totalBars);
        const auto x = xForBeats (beats);

        if (x < (float) area.getX() - 1.0f)
            continue;

        if (x > (float) area.getRight())
            break;

        // Alternate shading every bar helps reading long songs.
        if (bar % 2 == 0)
        {
            const auto nextBeats = beats + m_tempoMap->barLengthInBeats (juce::jmin (bar, totalBars));
            g.setColour (juce::Colours::white.withAlpha (0.012f));
            g.fillRect (juce::Rectangle<float> (x, (float) area.getY(), xForBeats (nextBeats) - x, (float) area.getHeight()));
        }

        g.setColour ((strong ? palette.panelBorder : palette.panelBorder.withAlpha (0.55f)));
        g.fillRect (juce::Rectangle<float> (x, (float) area.getY(), 1.0f, (float) area.getHeight()));
    }
}

void ArrangementView::paintPastEnd (juce::Graphics& g, juce::Rectangle<int> area)
{
    auto* song = m_session.getSelectedSong();

    if (song == nullptr)
        return;

    // Outside the locators is outside what plays
    const auto endX = xForBeats (m_tempoMap->toBeats (song->getRightLocator()));

    if (song->getLeftLocator() != Bar {})
    {
        const auto startX = juce::jmin (xForBeats (m_tempoMap->toBeats (song->getLeftLocator())), (float) area.getRight());

        if (startX > (float) area.getX())
        {
            g.setColour (juce::Colours::black.withAlpha (0.35f));
            g.fillRect (juce::Rectangle<float> ((float) area.getX(), (float) area.getY(), startX - (float) area.getX(), (float) area.getHeight()));
        }
    }

    if (endX < (float) area.getRight())
    {
        g.setColour (juce::Colours::black.withAlpha (0.35f));
        g.fillRect (juce::Rectangle<float> (juce::jmax (endX, (float) area.getX()), (float) area.getY(),
                                            (float) area.getRight() - juce::jmax (endX, (float) area.getX()), (float) area.getHeight()));
    }
}

void ArrangementView::paintRuler (juce::Graphics& g, juce::Rectangle<int> area)
{
    const auto& palette = ui::getTheme (*this).getPalette();
    auto* song = m_session.getSelectedSong();

    g.setGradientFill (juce::ColourGradient (palette.capTop.darker (0.2f), 0.0f, (float) area.getY(),
                                             palette.capBottom, 0.0f, (float) area.getBottom(), false));
    g.fillRect (area);

    // Song range: a band like Logic's cycle/arrangement area, from the start bar to the end.
    const auto endX = xForBeats (m_tempoMap->toBeats (song->getRightLocator()));
    const auto startX = xForBeats (m_tempoMap->toBeats (song->getLeftLocator()));
    const auto band = juce::Rectangle<float> (juce::jmax ((float) area.getX(), startX), (float) area.getY() + 2.0f,
                                              0.0f, 14.0f).withRight (juce::jmin ((float) area.getRight(), endX));

    if (band.getWidth() > 0.0f)
    {
        g.setColour (palette.accent.withAlpha (0.22f));
        g.fillRoundedRectangle (band, 3.0f);
    }

    juce::Graphics::ScopedSaveState state (g);
    g.reduceClipRegion (area);

    // Label every N bars so numbers never collide.
    const auto barWidth = m_pixelsPerBeat * 4.0;
    int step = 1;

    while (barWidth * step < 34.0)
        step *= 2;

    g.setFont (ui::LookAndFeel::makeFont (12.0f, true));

    const auto totalBars = song->getTotalBar();

    for (int bar = 1; bar <= totalBars + (int) extraBarsPastEnd; ++bar)
    {
        const auto inSong = bar <= totalBars;
        const auto beats = inSong ? m_tempoMap->toBeats (Bar { bar, 1, 1, 0 })
                                  : m_tempoMap->getLengthInBeats() + (bar - totalBars - 1) * m_tempoMap->barLengthInBeats (totalBars);
        const auto x = xForBeats (beats);

        if (x > (float) area.getRight())
            break;

        const auto signatureBar = juce::jmin (bar, totalBars);
        const auto numerator = m_tempoMap->signatureAt (signatureBar).m_numerator;
        const auto beatUnit = m_tempoMap->beatUnitLengthInBeats (signatureBar);

        // Beat-unit ticks when zoomed in enough
        if (m_pixelsPerBeat * beatUnit >= 7.0)
        {
            g.setColour (palette.textDim.withAlpha (0.6f));

            for (int beat = 1; beat < numerator; ++beat)
                g.fillRect (juce::Rectangle<float> (xForBeats (beats + beat * beatUnit), (float) area.getBottom() - 6.0f, 1.0f, 6.0f));
        }

        const auto labelled = (bar - 1) % step == 0;
        g.setColour (labelled ? palette.textDim.brighter (0.5f) : palette.textDim);
        g.fillRect (juce::Rectangle<float> (x, (float) area.getBottom() - (labelled ? 16.0f : 9.0f), 1.0f, labelled ? 16.0f : 9.0f));

        if (labelled)
        {
            g.setColour (inSong ? palette.text.brighter (0.3f) : palette.textDim);
            g.drawText (juce::String (bar), juce::Rectangle<float> (x + 4.0f, (float) area.getY() + 2.0f, 50.0f, 16.0f),
                        juce::Justification::centredLeft, false);
        }
    }

    // The locators. The end marker's label goes left of its line, so a ruler number never hides it.
    g.setFont (ui::LookAndFeel::makeFont (10.0f, true).withExtraKerningFactor (0.1f));

    if (song->getLeftLocator() != Bar {} && startX >= (float) area.getX() && startX <= (float) area.getRight())
    {
        g.setColour (palette.accentWarm);
        g.fillRect (juce::Rectangle<float> (startX - 1.0f, (float) area.getY(), 2.0f, (float) area.getHeight()));
        g.drawText ("START", juce::Rectangle<float> (startX + 4.0f, (float) area.getBottom() - 15.0f, 40.0f, 12.0f),
                    juce::Justification::centredLeft, false);
    }

    if (endX >= (float) area.getX() && endX <= (float) area.getRight())
    {
        g.setColour (palette.accentWarm);
        g.fillRect (juce::Rectangle<float> (endX - 1.0f, (float) area.getY(), 2.0f, (float) area.getHeight()));
        g.drawText ("END", juce::Rectangle<float> (endX - 34.0f, (float) area.getBottom() - 15.0f, 30.0f, 12.0f),
                    juce::Justification::centredRight, false);
    }
}

void ArrangementView::paintTempoLane (juce::Graphics& g, juce::Rectangle<int> header, juce::Rectangle<int> lane)
{
    const auto& palette = ui::getTheme (*this).getPalette();
    auto* song = m_session.getSelectedSong();
    const auto& grid = song->getTempoGrid();

    // Scale: round the tempo range out to tens, at least 40 BPM tall, like Logic's 90..200.
    int lo = grid.front().second, hi = lo;

    for (const auto& [bar, bpm] : grid)
    {
        lo = juce::jmin (lo, bpm);
        hi = juce::jmax (hi, bpm);
    }

    lo = (lo - 10) / 10 * 10;
    hi = (hi + 19) / 10 * 10;

    if (hi - lo < 40)
    {
        const auto centre = (hi + lo) / 2;
        lo = centre - 20;
        hi = centre + 20;
    }

    const auto plot = lane.toFloat().reduced (0.0f, 12.0f);
    auto yFor = [&] (int bpm) { return plot.getBottom() - (float) (bpm - lo) / (float) (hi - lo) * plot.getHeight(); };

    // Header: name and scale
    g.setColour (palette.text.brighter (0.2f));
    g.setFont (ui::LookAndFeel::makeFont (13.0f, true));
    g.drawText ("Tempo", header.reduced (14, 0).withHeight (26).translated (0, 6), juce::Justification::centredLeft, false);

    g.setFont (ui::LookAndFeel::makeFont (10.0f));
    g.setColour (palette.textDim.brighter (0.3f));

    for (const auto value : { hi, (hi + lo) / 2, lo })
        g.drawText (juce::String (value), juce::Rectangle<float> ((float) header.getRight() - 46.0f, yFor (value) - 6.0f, 38.0f, 12.0f),
                    juce::Justification::centredRight, false);

    // Lane
    g.setColour (palette.bgBottom.darker (0.25f));
    g.fillRect (lane);

    juce::Graphics::ScopedSaveState state (g);
    g.reduceClipRegion (lane);
    paintGrid (g, lane, false);

    for (const auto value : { hi, (hi + lo) / 2, lo })
    {
        g.setColour (juce::Colours::white.withAlpha (0.035f));
        g.fillRect (juce::Rectangle<float> ((float) lane.getX(), yFor (value), (float) lane.getWidth(), 1.0f));
    }

    // The step curve: flat at each tempo, vertical at each change.
    const auto endBeats = m_tempoMap->getLengthInBeats();
    juce::Path line, fill;

    for (size_t i = 0; i < grid.size(); ++i)
    {
        const auto x = xForBeats (m_tempoMap->toBeats (grid[i].first));
        const auto nextX = xForBeats (i + 1 < grid.size() ? m_tempoMap->toBeats (grid[i + 1].first) : endBeats);
        const auto y = yFor (grid[i].second);

        if (i == 0)
        {
            line.startNewSubPath (x, y);
            fill.startNewSubPath (x, plot.getBottom() + 12.0f);
            fill.lineTo (x, y);
        }
        else
        {
            line.lineTo (x, y);
            fill.lineTo (x, y);
        }

        line.lineTo (nextX, y);
        fill.lineTo (nextX, y);
    }

    fill.lineTo (xForBeats (endBeats), plot.getBottom() + 12.0f);
    fill.closeSubPath();

    g.setGradientFill (juce::ColourGradient (palette.accent.withAlpha (0.28f), 0.0f, plot.getY(),
                                             palette.accent.withAlpha (0.08f), 0.0f, plot.getBottom(), false));
    g.fillPath (fill);

    g.setColour (palette.accent.withAlpha (0.35f));
    g.strokePath (line, juce::PathStrokeType (4.0f));
    g.setColour (palette.accent);
    g.strokePath (line, juce::PathStrokeType (1.6f));

    // Points and values at every change.
    g.setFont (ui::LookAndFeel::makeFont (11.0f, true));

    for (const auto& [bar, bpm] : grid)
    {
        const auto point = juce::Point<float> (xForBeats (m_tempoMap->toBeats (bar)), yFor (bpm));
        g.setColour (palette.accent.brighter (0.4f));
        g.fillEllipse (juce::Rectangle<float> (7.0f, 7.0f).withCentre (point));
        g.setColour (palette.text.brighter (0.4f));
        g.drawText (juce::String (bpm), juce::Rectangle<float> (point.x + 6.0f, point.y + 3.0f, 44.0f, 14.0f),
                    juce::Justification::centredLeft, false);
    }

    paintPastEnd (g, lane);
}

void ArrangementView::paintSignatureLane (juce::Graphics& g, juce::Rectangle<int> header, juce::Rectangle<int> lane)
{
    const auto& palette = ui::getTheme (*this).getPalette();
    auto* song = m_session.getSelectedSong();

    g.setColour (palette.text.brighter (0.2f));
    g.setFont (ui::LookAndFeel::makeFont (13.0f, true));
    g.drawText ("Signature", header.reduced (14, 0), juce::Justification::centredLeft, false);

    g.setColour (palette.bgBottom.darker (0.15f));
    g.fillRect (lane);

    juce::Graphics::ScopedSaveState state (g);
    g.reduceClipRegion (lane);
    paintGrid (g, lane, false);

    const auto& grid = song->getSignatureGrid();
    g.setFont (ui::LookAndFeel::makeFont (13.0f, true));

    for (size_t i = 0; i < grid.size(); ++i)
    {
        const auto x = xForBeats (m_tempoMap->toBeats (grid[i].first));
        const auto nextX = xForBeats (i + 1 < grid.size() ? m_tempoMap->toBeats (grid[i + 1].first) : m_tempoMap->getLengthInBeats());
        const auto block = juce::Rectangle<float> (x + 1.0f, (float) lane.getY() + 4.0f, nextX - x - 2.0f, (float) lane.getHeight() - 8.0f);

        g.setColour (palette.capBottom.withAlpha (0.8f));
        g.fillRoundedRectangle (block, 3.0f);
        g.setColour (palette.accent);
        g.fillRect (block.withWidth (2.0f));
        g.setColour (palette.text.brighter (0.3f));
        g.drawText (UIHelpers::signatureText (grid[i].second), block.withTrimmedLeft (8.0f), juce::Justification::centredLeft, true);
    }

    paintPastEnd (g, lane);
}

void ArrangementView::paintTracks (juce::Graphics& g)
{
    auto* song = m_session.getSelectedSong();
    const auto area = juce::Rectangle<int> (0, tracksTop(), getWidth() - scrollBarSize, getHeight() - tracksTop() - scrollBarSize);

    juce::Graphics::ScopedSaveState state (g);
    g.reduceClipRegion (area);

    const auto& tracks = song->getTracks();

    for (size_t i = 0; i < tracks.size(); ++i)
    {
        const auto row = trackRowBounds ((int) i);

        if (row.getBottom() < area.getY() || row.getY() > area.getBottom())
            continue;

        const auto selected = tracks[i]->getId() == m_session.getSelectedTrackId();
        paintTrackHeader (g, row.withWidth (headerWidth), *tracks[i], (int) i, selected);
        paintTrackLane (g, row.withTrimmedLeft (headerWidth), *tracks[i], selected);
    }

    // Empty space under the last track
    const auto lastBottom = trackRowBounds ((int) tracks.size()).getY();

    if (lastBottom < area.getBottom())
    {
        auto empty = area.withTop (lastBottom).withTrimmedLeft (headerWidth);
        juce::Graphics::ScopedSaveState emptyState (g);
        g.reduceClipRegion (empty);
        paintGrid (g, empty, false);
        paintPastEnd (g, empty);
    }
}

void ArrangementView::paintTrackHeader (juce::Graphics& g, juce::Rectangle<int> area, const MBoxTrack& track, int index, bool selected)
{
    const auto& palette = ui::getTheme (*this).getPalette();
    auto bounds = area.toFloat();

    g.setColour (selected ? palette.capTop.withAlpha (0.55f) : (index % 2 == 0 ? palette.panelFill : palette.panelFill.brighter (0.02f)));
    g.fillRect (bounds);
    g.setColour (juce::Colours::black.withAlpha (0.45f));
    g.fillRect (bounds.withTop (bounds.getBottom() - 1.0f));

    if (selected)
    {
        g.setColour (palette.accent);
        g.fillRect (bounds.withWidth (3.0f));
    }

    auto content = bounds.reduced (10.0f, 8.0f);

    // Track number
    g.setColour (palette.textDim.brighter (0.4f));
    g.setFont (ui::LookAndFeel::makeFont (13.0f, true));
    g.drawText (juce::String (index + 1), content.removeFromLeft (22.0f), juce::Justification::centredLeft, false);

    // Type tile
    const auto type = track.getType();
    const auto isWav = type == TrackType::Audio;
    const auto tile = content.removeFromLeft (40.0f).withSizeKeepingCentre (40.0f, 40.0f);
    g.setGradientFill (juce::ColourGradient (palette.capTop, tile.getX(), tile.getY(), palette.capBottom, tile.getX(), tile.getBottom(), false));
    g.fillRoundedRectangle (tile, 6.0f);
    g.setColour (juce::Colours::black.withAlpha (0.5f));
    g.drawRoundedRectangle (tile, 6.0f, 1.0f);

    auto icon = isWav ? makeWaveIcon() : type == TrackType::Physical ? makeInputIcon() : makeInfoIcon();
    icon.applyTransform (icon.getTransformToScaleToFit (tile.reduced (10.0f), true));
    g.setColour (isWav ? palette.accent : type == TrackType::Physical ? palette.accentWarm : palette.textDim.brighter (0.5f));
    g.fillPath (icon);

    content.removeFromLeft (10.0f);

    // Name, type and source
    auto nameArea = content.removeFromTop (content.getHeight() * 0.5f).withTrimmedRight (56.0f);
    // Volume and mute / solo in the bus the mixer shows
    auto* song = m_session.getSelectedSong();
    const auto* trackMix = m_session.getTrackMix (track.getId());
    const auto audible = song != nullptr && UIHelpers::isAudible (*song, m_session.getSelectedBus(), track.getId());
    g.setColour (audible ? palette.text.brighter (selected ? 0.5f : 0.3f) : palette.textDim);
    g.setFont (ui::LookAndFeel::makeFont (14.0f, true));
    g.drawText (track.getName(), nameArea, juce::Justification::bottomLeft, true);

    juce::String detail;

    if (const auto* audio = dynamic_cast<const MBoxAudioTrack*> (&track))
        detail = audio->getFilename() != juce::File() ? audio->getFilename().getFileName() : "No WAV";
    else if (const auto* physical = dynamic_cast<const MBoxPhysicalTrack*> (&track))
        detail = "Input " + juce::String (physical->getPhysicalInput() + 1);
    else
        detail = "Cue clicks";

    g.setColour (palette.textDim.brighter (0.3f));
    g.setFont (ui::LookAndFeel::makeFont (11.5f));
    g.drawText (detail, content.withTrimmedRight (64.0f), juce::Justification::topLeft, true);

    if (trackMix == nullptr)
        return;

    g.setColour (palette.textDim.brighter (0.2f));
    g.drawText (UIHelpers::volumeText (trackMix->m_volume), content, juce::Justification::topRight, false);

    paintHeaderButton (g, headerButtonBounds (area, HeaderButton::mute), "M", trackMix->m_mute, palette.accent);
    paintHeaderButton (g, headerButtonBounds (area, HeaderButton::solo), "S", trackMix->m_solo, palette.solo);
}

juce::Rectangle<float> ArrangementView::headerButtonBounds (juce::Rectangle<int> row, HeaderButton button) const
{
    constexpr float size = 24.0f, gap = 2.0f;
    const auto buttons = row.withWidth (headerWidth).toFloat().reduced (10.0f, 8.0f).removeFromTop (20.0f);
    const auto index = (float) static_cast<int> (button);
    return buttons.withX (buttons.getRight() - size - index * (size + gap)).withWidth (size);
}

std::optional<ArrangementView::HeaderButton> ArrangementView::headerButtonAt (juce::Rectangle<int> row, juce::Point<float> position) const
{
    for (auto button : { HeaderButton::solo, HeaderButton::mute })
    {
        if (headerButtonBounds (row, button).contains (position))
            return button;
    }

    return std::nullopt;
}

void ArrangementView::paintHeaderButton (juce::Graphics& g, juce::Rectangle<float> area, const juce::String& text, bool on, juce::Colour onColour)
{
    const auto& palette = ui::getTheme (*this).getPalette();
    const auto base = on ? onColour : palette.capBottom;

    g.setGradientFill (juce::ColourGradient (base.brighter (0.12f), 0.0f, area.getY(), base.darker (0.2f), 0.0f, area.getBottom(), false));
    g.fillRoundedRectangle (area, 4.0f);
    g.setColour (juce::Colours::black.withAlpha (0.5f));
    g.drawRoundedRectangle (area, 4.0f, 1.0f);
    g.setColour (on ? palette.textOnAccent : palette.text);
    g.setFont (ui::LookAndFeel::makeFont (11.0f, true));
    g.drawText (text, area, juce::Justification::centred, false);
}

void ArrangementView::paintTrackLane (juce::Graphics& g, juce::Rectangle<int> area, const MBoxTrack& track, bool selected)
{
    const auto& palette = ui::getTheme (*this).getPalette();

    g.setColour (selected ? palette.panelFill.brighter (0.05f) : palette.bgBottom.darker (0.1f));
    g.fillRect (area);

    juce::Graphics::ScopedSaveState state (g);
    g.reduceClipRegion (area);
    paintGrid (g, area, false);

    auto* song = m_session.getSelectedSong();
    const auto greyed = song == nullptr || ! UIHelpers::isAudible (*song, m_session.getSelectedBus(), track.getId());

    if (const auto* audio = dynamic_cast<const MBoxAudioTrack*> (&track))
        paintWavRegion (g, area, *audio, greyed);
    else if (const auto* physical = dynamic_cast<const MBoxPhysicalTrack*> (&track))
        paintBankRegions (g, area, *physical, greyed);
    else if (const auto* info = dynamic_cast<const MBoxInfoTrack*> (&track))
        paintInfoClicks (g, area, *info, greyed);

    paintPastEnd (g, area);

    g.setColour (juce::Colours::black.withAlpha (0.45f));
    g.fillRect (area.withTop (area.getBottom() - 1));
}

juce::File ArrangementView::wavFileFor (const MBoxAudioTrack& track)
{
    return track.getFilename();
}

juce::AudioThumbnail* ArrangementView::getThumbnail (const juce::File& file)
{
    const auto key = file.getFullPathName();

    if (auto it = m_thumbnails.find (key); it != m_thumbnails.end())
        return it->second.get();

    if (! file.existsAsFile())
        return nullptr;

    auto thumbnail = std::make_unique<juce::AudioThumbnail> (256, m_formats, m_thumbnailCache);
    thumbnail->setSource (new juce::FileInputSource (file));
    thumbnail->addChangeListener (this);
    return m_thumbnails.emplace (key, std::move (thumbnail)).first->second.get();
}

double ArrangementView::getWavLengthSeconds (const juce::File& file)
{
    auto* thumbnail = getThumbnail (file);
    return thumbnail != nullptr ? thumbnail->getTotalLength() : 0.0;
}

juce::Rectangle<float> ArrangementView::wavRegionBounds (juce::Rectangle<int> lane, const MBoxAudioTrack& track, const Bar& start)
{
    const auto file = wavFileFor (track);
    const auto startBeats = m_tempoMap->toBeats (start);
    const auto startSeconds = m_tempoMap->toSeconds (start);
    const auto endBeats = file.existsAsFile() ? m_tempoMap->secondsToBeats (startSeconds + getWavLengthSeconds (file))
                                              : startBeats + 4.0;   // a placeholder width for a missing file

    return { xForBeats (startBeats), (float) lane.getY() + 4.0f, xForBeats (endBeats) - xForBeats (startBeats), (float) lane.getHeight() - 9.0f };
}

const MBoxAudioTrack* ArrangementView::wavRegionAt (juce::Point<float> p)
{
    const auto hit = hitAt (p);
    auto* song = m_session.getSelectedSong();

    if (hit.zone != Zone::TrackLane || song == nullptr || m_tempoMap == nullptr)
        return nullptr;

    const auto* track = dynamic_cast<const MBoxAudioTrack*> (song->getTracks()[(size_t) hit.trackIndex].get());

    if (track == nullptr || track->getFilename() == juce::File())
        return nullptr;

    const auto lane = trackRowBounds (hit.trackIndex).withTrimmedLeft (headerWidth);
    return wavRegionBounds (lane, *track, track->getAudioStart()).contains (p) ? track : nullptr;
}

void ArrangementView::paintWavRegion (juce::Graphics& g, juce::Rectangle<int> lane, const MBoxAudioTrack& track, bool greyed)
{
    const auto& palette = ui::getTheme (*this).getPalette();

    if (track.getFilename() == juce::File())
    {
        g.setColour (palette.textDim);
        g.setFont (ui::LookAndFeel::makeFont (12.0f));
        g.drawText ("Double-click to place a WAV", lane.reduced (12, 0).withX ((int) juce::jmax ((float) lane.getX() + 12.0f, xForBeats (0.0) + 12.0f)),
                    juce::Justification::centredLeft, false);
        return;
    }

    const auto file = wavFileFor (track);
    const auto dragging = m_wavDrag.has_value() && m_wavDrag->trackId == track.getId();
    const auto start = dragging ? m_wavDrag->preview : track.getAudioStart();
    const auto startBeats = m_tempoMap->toBeats (start);
    const auto startSeconds = m_tempoMap->toSeconds (start);
    const auto missing = ! file.existsAsFile();
    const auto region = wavRegionBounds (lane, track, start);
    const auto endBeats = beatsForX (region.getRight());
    // Muted, or silenced by another track's solo: grey, like Logic.
    const auto colour = greyed ? UIHelpers::mutedRegionColour() : missing ? palette.accentWarm : palette.accent;

    if (dragging)
    {
        // Ghost at the original position, and the position the region would land on.
        g.setColour (palette.text.withAlpha (0.25f));
        g.drawRoundedRectangle (wavRegionBounds (lane, track, m_wavDrag->original).reduced (0.5f), 4.0f, 1.0f);

        g.setColour (palette.accent.brighter (0.3f));
        g.setFont (ui::LookAndFeel::makeFont (11.5f, true));
        g.drawText (UIHelpers::positionText (start), juce::Rectangle<float> (region.getX(), region.getBottom() - 15.0f, 120.0f, 14.0f),
                    juce::Justification::centredLeft, false);
    }

    g.setColour (colour.withAlpha (0.55f).darker (0.35f));
    g.fillRoundedRectangle (region, 4.0f);
    g.setColour (colour.withAlpha (0.9f));
    g.drawRoundedRectangle (region.reduced (0.5f), 4.0f, 1.0f);

    // Name strip
    auto title = region.withHeight (17.0f);
    g.setColour (colour.withAlpha (0.85f));
    g.fillRoundedRectangle (title, 4.0f);
    g.fillRect (title.withTrimmedTop (8.0f));
    g.setColour (palette.textOnAccent);
    g.setFont (ui::LookAndFeel::makeFont (11.5f, true));
    g.drawText (file.getFileName() + (missing ? "  (missing)" : ""), title.reduced (6.0f, 0.0f), juce::Justification::centredLeft, true);

    // Waveform, drawn per constant-tempo segment so it lines up with the beat grid.
    if (auto* thumbnail = getThumbnail (file); thumbnail != nullptr && thumbnail->getTotalLength() > 0.0)
    {
        const auto waveArea = region.withTrimmedTop (18.0f).reduced (2.0f, 2.0f);
        auto* song = m_session.getSelectedSong();
        std::vector<double> boundaries { startBeats };

        for (const auto& [bar, bpm] : song->getTempoGrid())
            if (const auto b = m_tempoMap->toBeats (bar); b > startBeats && b < endBeats)
                boundaries.push_back (b);

        const auto waveColour = greyed ? palette.textDim.brighter (0.3f) : palette.text.brighter (0.6f).withAlpha (0.85f);

        // Cached tiles once the thumbnail is complete; while it still loads, drawn directly.
        if (thumbnail->isFullyLoaded())
        {
            paintWaveform (g, *thumbnail, track, waveArea, lane, startBeats, startSeconds, endBeats, waveColour);
            return;
        }

        boundaries.push_back (endBeats);
        g.setColour (waveColour);

        for (size_t i = 0; i + 1 < boundaries.size(); ++i)
        {
            // Only the visible part: zoomed in, a whole segment can be tens of thousands of pixels
            // wide, and the thumbnail would compute every one of them on each frame of a zoom drag.
            const auto x0 = juce::jmax ((float) lane.getX() - 1.0f, xForBeats (boundaries[i]));
            const auto x1 = juce::jmin ((float) lane.getRight() + 1.0f, xForBeats (boundaries[i + 1]));

            if (x1 <= x0)
                continue;

            const auto t0 = m_tempoMap->beatsToSeconds (beatsForX (x0)) - startSeconds;
            const auto t1 = m_tempoMap->beatsToSeconds (beatsForX (x1)) - startSeconds;
            thumbnail->drawChannels (g, juce::Rectangle<float> (x0, waveArea.getY(), x1 - x0, waveArea.getHeight()).toNearestInt(),
                                     t0, t1, 0.9f);
        }
    }
}

void ArrangementView::paintWaveform (juce::Graphics& g, juce::AudioThumbnail& thumbnail, const MBoxAudioTrack& track,
                                     juce::Rectangle<float> waveArea, juce::Rectangle<int> lane,
                                     double startBeats, double startSeconds, double endBeats, juce::Colour colour)
{
    constexpr auto tileWidth = WaveTiles::width;
    const auto height = juce::roundToInt (waveArea.getHeight());
    const auto regionX = juce::roundToInt (xForBeats (startBeats));
    const auto regionWidth = (double) (endBeats - startBeats) * m_pixelsPerBeat;

    if (height <= 0 || regionWidth <= 0.0)
        return;

    // Tiles are in pixels from the region's start, so scrolling only moves them.
    juce::String key;
    key << wavFileFor (track).getFullPathName() << '|' << m_pixelsPerBeat << '|' << height << '|' << (juce::int64) colour.getARGB()
        << '|' << startBeats;

    for (const auto& [bar, bpm] : m_session.getSelectedSong()->getTempoGrid())
        key << '|' << m_tempoMap->toBeats (bar) << '@' << bpm;

    auto& cache = m_waveTiles[track.getId()];

    if (cache.key != key)
    {
        cache.key = key;
        cache.tiles.clear();
    }

    const auto scale = juce::jmax (1.0f, g.getInternalContext().getPhysicalPixelScaleFactor());
    const auto visibleFrom = juce::jmax (0, lane.getX() - regionX);
    const auto visibleTo = juce::jmin ((int) std::ceil (regionWidth), lane.getRight() - regionX);
    const auto firstTile = visibleFrom / tileWidth;
    const auto lastTile = visibleTo / tileWidth;

    // Playing through a long song zoomed in leaves tiles behind: keep only those near the view.
    if (cache.tiles.size() > WaveTiles::maxTiles)
        std::erase_if (cache.tiles, [&] (const auto& entry) { return entry.first < firstTile - 4 || entry.first > lastTile + 4; });

    const juce::Graphics::ScopedSaveState state (g);
    g.reduceClipRegion (lane);

    for (auto tile = firstTile; tile <= lastTile; ++tile)
    {
        auto& image = cache.tiles[tile];

        if (! image.isValid())
        {
            image = juce::Image (juce::Image::ARGB, juce::roundToInt ((float) tileWidth * scale), juce::roundToInt ((float) height * scale), true);
            juce::Graphics tileGraphics (image);
            tileGraphics.addTransform (juce::AffineTransform::scale (scale));
            tileGraphics.setColour (colour);

            // Per constant-tempo segment, so the waveform lines up with the beat grid.
            const auto tileStart = tile * tileWidth;
            std::vector<double> boundaries { (double) tileStart, (double) tileStart + tileWidth };

            for (const auto& [bar, bpm] : m_session.getSelectedSong()->getTempoGrid())
                if (const auto x = (m_tempoMap->toBeats (bar) - startBeats) * m_pixelsPerBeat; x > tileStart && x < tileStart + tileWidth)
                    boundaries.push_back (x);

            std::sort (boundaries.begin(), boundaries.end());

            for (size_t i = 0; i + 1 < boundaries.size(); ++i)
            {
                const auto x0 = boundaries[i], x1 = juce::jmin (boundaries[i + 1], regionWidth);

                if (x1 <= x0)
                    continue;

                const auto t0 = m_tempoMap->beatsToSeconds (startBeats + x0 / m_pixelsPerBeat) - startSeconds;
                const auto t1 = m_tempoMap->beatsToSeconds (startBeats + x1 / m_pixelsPerBeat) - startSeconds;
                thumbnail.drawChannels (tileGraphics, juce::Rectangle<double> (x0 - tileStart, 0.0, x1 - x0, height).toNearestInt(),
                                        t0, t1, 0.9f);
            }
        }

        g.drawImage (image, juce::Rectangle<float> ((float) (regionX + tile * tileWidth), waveArea.getY(), (float) tileWidth, (float) height));
    }
}

void ArrangementView::paintBankRegions (juce::Graphics& g, juce::Rectangle<int> lane, const MBoxPhysicalTrack& track, bool greyed)
{
    const auto& palette = ui::getTheme (*this).getPalette();
    const auto& grid = track.getBankSlotChanges();
    const auto banks = m_session.getBanks();

    if (grid.empty())
    {
        g.setColour (palette.textDim);
        g.setFont (ui::LookAndFeel::makeFont (12.0f));
        g.drawText ("Dry  -  double-click to switch to a bankslot here",
                    lane.reduced (12, 0).withX ((int) juce::jmax ((float) lane.getX() + 12.0f, xForBeats (0.0) + 12.0f)),
                    juce::Justification::centredLeft, false);
        return;
    }

    // Active region while playing, so the switch is visible as it happens.
    const auto playBeats = activePlayBeats();

    for (size_t i = 0; i < grid.size(); ++i)
    {
        const auto& [bar, ref] = grid[i];
        const auto startBeats = m_tempoMap->toBeats (bar);
        const auto endBeats = i + 1 < grid.size() ? m_tempoMap->toBeats (grid[i + 1].first) : m_tempoMap->getLengthInBeats();
        const auto region = juce::Rectangle<float> (xForBeats (startBeats) + 1.0f, (float) lane.getY() + 4.0f,
                                                    xForBeats (endBeats) - xForBeats (startBeats) - 2.0f, (float) lane.getHeight() - 9.0f);

        if (region.getRight() < (float) lane.getX() || region.getX() > (float) lane.getRight())
            continue;

        const auto colour = greyed ? UIHelpers::mutedRegionColour() : UIHelpers::bankColour (ref.m_bank);
        const auto active = playBeats >= startBeats && playBeats < endBeats;

        g.setColour (colour.withAlpha (active ? 0.75f : 0.5f).darker (0.4f));
        g.fillRoundedRectangle (region, 4.0f);
        g.setColour (active ? colour.brighter (0.4f) : colour.withAlpha (0.9f));
        g.drawRoundedRectangle (region.reduced (0.5f), 4.0f, active ? 2.0f : 1.0f);

        auto title = region.withHeight (17.0f);
        g.setColour (colour.withAlpha (0.9f));
        g.fillRoundedRectangle (title, 4.0f);
        g.fillRect (title.withTrimmedTop (8.0f));
        g.setColour (palette.textOnAccent);
        g.setFont (ui::LookAndFeel::makeFont (11.5f, true));
        g.drawText (UIHelpers::bankSlotName (banks, ref), title.reduced (6.0f, 0.0f), juce::Justification::centredLeft, true);

        g.setColour (palette.text.brighter (0.4f));
        g.setFont (ui::LookAndFeel::makeFont (11.5f));
        g.drawText (UIHelpers::bankSlotEffects (banks, ref, juce::String (juce::CharPointer_UTF8 (" \xe2\x80\xba "))),
                    region.withTrimmedTop (19.0f).reduced (7.0f, 2.0f), juce::Justification::topLeft, true);
    }
}

void ArrangementView::paintInfoClicks (juce::Graphics& g, juce::Rectangle<int> lane, const MBoxInfoTrack& track, bool greyed)
{
    const auto& palette = ui::getTheme (*this).getPalette();
    const auto& clicks = track.getInfoClicks();

    if (clicks.empty())
    {
        g.setColour (palette.textDim);
        g.setFont (ui::LookAndFeel::makeFont (12.0f));
        g.drawText ("No cue clicks  -  double-click to add one here",
                    lane.reduced (12, 0).withX ((int) juce::jmax ((float) lane.getX() + 12.0f, xForBeats (0.0) + 12.0f)),
                    juce::Justification::centredLeft, false);
        return;
    }

    // Each entry plays one bar of clicks (a click per beat-unit, the first accented)
    const auto colour = greyed ? UIHelpers::mutedRegionColour() : palette.textDim.brighter (0.5f);

    for (const auto& position : clicks)
    {
        const auto startBeats = m_tempoMap->toBeats (position);
        const auto signature = m_tempoMap->signatureAt (position.m_bar);
        const auto beatUnit = (double) signature.ticksPerBeatUnit() / ticksPerQuarterNote;
        const auto region = juce::Rectangle<float> (xForBeats (startBeats), (float) lane.getY() + 4.0f,
                                                    (float) (signature.m_numerator * beatUnit * m_pixelsPerBeat), (float) lane.getHeight() - 9.0f);

        if (region.getRight() < (float) lane.getX() || region.getX() > (float) lane.getRight())
            continue;

        g.setColour (colour.withAlpha (0.2f));
        g.fillRoundedRectangle (region, 3.0f);
        g.setColour (colour);
        g.fillRect (region.withWidth (2.0f));

        // One dot per click, the first bigger, sized to the space a beat-unit gets
        const auto beatPixels = (float) (beatUnit * m_pixelsPerBeat);

        for (int beat = 0; beat < signature.m_numerator; ++beat)
        {
            const auto size = juce::jlimit (2.0f, beat == 0 ? 9.0f : 6.0f, beatPixels * (beat == 0 ? 0.8f : 0.55f));
            const auto x = xForBeats (startBeats + (beat + 0.5) * beatUnit);
            g.fillEllipse (juce::Rectangle<float> (size, size).withCentre ({ x, region.getCentreY() }));
        }
    }
}

void ArrangementView::paintPlayheadAndCursor (juce::Graphics& g)
{
    const auto& palette = ui::getTheme (*this).getPalette();
    const auto timeline = timelineArea();

    juce::Graphics::ScopedSaveState state (g);
    g.reduceClipRegion (timeline);

    // Edit cursor: a thin accent line with a small flag in the ruler.
    const auto cursorX = xForBeats (m_tempoMap->toBeats (m_session.getCursor()));
    g.setColour (palette.accent.withAlpha (0.45f));
    g.fillRect (juce::Rectangle<float> (cursorX, (float) rulerHeight, 1.0f, (float) (timeline.getHeight() - rulerHeight)));
    juce::Path flag;
    flag.addTriangle (cursorX - 5.0f, (float) rulerHeight - 9.0f, cursorX + 5.0f, (float) rulerHeight - 9.0f, cursorX, (float) rulerHeight - 2.0f);
    g.setColour (palette.accent);
    g.fillPath (flag);

    // Playhead: white line with a triangular head, like Logic.
    const auto x = playheadX();

    if (x >= 0.0f)
    {
        g.setColour (juce::Colours::white.withAlpha (0.25f));
        g.fillRect (juce::Rectangle<float> (x - 1.5f, 0.0f, 3.0f, (float) timeline.getHeight()));
        g.setColour (juce::Colours::white.withAlpha (0.9f));
        g.fillRect (juce::Rectangle<float> (x - 0.5f, 0.0f, 1.0f, (float) timeline.getHeight()));

        juce::Path head;
        head.addTriangle (x - 7.0f, (float) rulerHeight - 16.0f, x + 7.0f, (float) rulerHeight - 16.0f, x, (float) rulerHeight - 4.0f);
        g.fillPath (head);
    }
}

//==============================================================================
void ArrangementView::mouseDown (const juce::MouseEvent& e)
{
    if (m_tempoMap == nullptr)
        return;

    const auto hit = hitAt (e.position);
    auto* song = m_session.getSelectedSong();

    if (auto* track = wavRegionAt (e.position); track != nullptr && ! e.mods.isPopupMenu())
    {
        m_session.selectTrack (track->getId());

        if (m_session.canEdit())
        {
            const auto start = track->getAudioStart();
            m_wavDrag = WavDrag { track->getId(), start, start, hit.beats - m_tempoMap->toBeats (start) };
            setMouseCursor (juce::MouseCursor::DraggingHandCursor);
        }

        return;
    }

    switch (hit.zone)
    {
        case Zone::Ruler:
        case Zone::TempoLane:
        case Zone::SignatureLane:
            m_session.setCursor (barAt (hit.beats, hit.zone == Zone::TempoLane));
            break;

        case Zone::TrackHeader:
        {
            const auto row = trackRowBounds (hit.trackIndex);
            const auto trackId = song->getTracks()[(size_t) hit.trackIndex]->getId();

            // Mute / solo (in the selected bus) work any time, like the mixer's
            const auto* trackMix = m_session.getTrackMix (trackId);
            const auto button = headerButtonAt (row, e.position);

            if (button == HeaderButton::mute && trackMix != nullptr)
                m_session.setTrackMute (trackId, ! trackMix->m_mute);
            else if (button == HeaderButton::solo && trackMix != nullptr)
                m_session.setTrackSolo (trackId, ! trackMix->m_solo);
            else
                m_session.selectTrack (trackId);

            break;
        }

        case Zone::TrackLane:
            m_session.selectTrack (song->getTracks()[(size_t) hit.trackIndex]->getId());
            m_session.setCursor (barAt (hit.beats, false));
            break;

        case Zone::None:
            break;
    }
}

void ArrangementView::mouseDrag (const juce::MouseEvent& e)
{
    if (! m_wavDrag.has_value() || m_tempoMap == nullptr)
        return;

    auto* song = m_session.getSelectedSong();

    if (song == nullptr)
        return;

    // A click without a real move must not shift the WAV by a rounding tick.
    if (e.getDistanceFromDragStart() < 3)
        return;

    // Smooth: the region follows the mouse to the tick (1/960 beat). Shift snaps to the nearest beat.
    const auto beats = juce::jlimit (0.0, m_tempoMap->toBeats (Bar { song->getTotalBar(), 1, 1, 0 }),
                                     beatsForX (e.position.x) - m_wavDrag->grabOffsetBeats);
    auto bar = m_tempoMap->beatsToBar (e.mods.isShiftDown() ? beats + 0.5 : beats);

    if (e.mods.isShiftDown())
        bar = Bar { bar.m_bar, bar.m_beat, 1, 0 };

    if (bar.m_bar > song->getTotalBar())
        bar = Bar { song->getTotalBar(), 1, 1, 0 };

    if (! (bar == m_wavDrag->preview))
    {
        m_wavDrag->preview = bar;
        repaint();
    }
}

void ArrangementView::mouseUp (const juce::MouseEvent&)
{
    if (! m_wavDrag.has_value())
        return;

    const auto drag = *m_wavDrag;
    m_wavDrag.reset();
    setMouseCursor (juce::MouseCursor::NormalCursor);

    if (drag.preview == drag.original)
    {
        repaint();
        return;
    }

    m_session.moveWav (drag.trackId, drag.preview);
}

void ArrangementView::mouseMove (const juce::MouseEvent& e)
{
    // No grab cursor while playback locks editing: the region can't be moved then.
    const auto overRegion = wavRegionAt (e.position) != nullptr && ! m_session.isEditLocked();
    setMouseCursor (overRegion ? juce::MouseCursor::DraggingHandCursor : juce::MouseCursor::NormalCursor);
}

void ArrangementView::mouseDoubleClick (const juce::MouseEvent& e)
{
    if (m_tempoMap == nullptr)
        return;

    const auto hit = hitAt (e.position);
    auto* song = m_session.getSelectedSong();

    if (hit.beats > m_tempoMap->getLengthInBeats() && hit.zone != Zone::TrackHeader)
        return;

    switch (hit.zone)
    {
        case Zone::TempoLane:
        {
            const auto position = barAt (hit.beats, true);
            const auto bpm = m_tempoMap->tempoAt (position);

            if (m_session.editSong ([&] (MBoxSong& s) { return s.addTempo ({ position, bpm }); },
                                    "Tempo change added at " + UIHelpers::positionText (position) + ": edit it in the Tempo list").wasOk())
                m_session.showInList (Session::ListTab::Tempo, position);
            break;
        }

        case Zone::SignatureLane:
        {
            const auto position = barAt (hit.beats, false);
            const auto signature = m_tempoMap->signatureAt (position.m_bar);

            if (m_session.editSong ([&] (MBoxSong& s) { return s.addSignature ({ position, signature }); },
                                    "Signature change added at bar " + juce::String (position.m_bar) + ": edit it in the Signature list").wasOk())
                m_session.showInList (Session::ListTab::Signature, position);
            break;
        }

        case Zone::TrackLane:
        {
            const auto& track = *song->getTracks()[(size_t) hit.trackIndex];
            const auto trackId = track.getId();

            if (track.getType() == TrackType::Audio)
            {
                chooseWavFor (trackId, barAt (hit.beats, false));
                break;
            }

            if (const auto* info = dynamic_cast<const MBoxInfoTrack*> (&track))
            {
                const auto position = barAt (hit.beats, true);
                juce::ignoreUnused (info);
                m_session.editTrack ([position] (MBoxTrack& t)
                                     {
                                         dynamic_cast<MBoxInfoTrack&> (t).addInfoClick (position);
                                         return juce::Result::ok();
                                     },
                                     "Cue clicks added at " + UIHelpers::positionText (position));
                break;
            }

            // Continue with the bankslot already active there, or Bank 1 / Slot 1
            const auto position = barAt (hit.beats, false);
            const auto ref = dynamic_cast<const MBoxPhysicalTrack&> (track).getSlotAt (position).value_or (MBoxSlotRef {});

            if (m_session.editTrack ([&] (MBoxTrack& t)
                                     {
                                         dynamic_cast<MBoxPhysicalTrack&> (t).setBankSlotChange (position, ref);
                                         return juce::Result::ok();
                                     },
                                     "Bankslot switch added at bar " + juce::String (position.m_bar) + ": pick the bankslot in the Bankslots list").wasOk())
                m_session.showInList (Session::ListTab::Bankslots, position);
            break;
        }

        case Zone::TrackHeader:
        {
            const auto row = trackRowBounds (hit.trackIndex);
            const auto& track = *song->getTracks()[(size_t) hit.trackIndex];

            // A quick double click on M or S just toggles twice; it doesn't rename
            if (headerButtonAt (row, e.position).has_value())
                break;

            const auto trackId = track.getId();
            m_session.showInList (Session::ListTab::Track, {});

            UIHelpers::askForText ("Rename track", "New name for " + track.getName() + ":", track.getName(), "Rename",
                                   [this, trackId] (const juce::String& name)
                                   {
                                       m_session.selectTrack (trackId);
                                       m_session.editTrack ([&name] (MBoxTrack& t) { t.setName (name); return juce::Result::ok(); },
                                                            "Track renamed to " + name);
                                   });
            break;
        }

        case Zone::Ruler:
        case Zone::None:
            break;
    }
}

void ArrangementView::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel)
{
    if (e.mods.isCommandDown() || e.mods.isCtrlDown())
    {
        setZoom (m_pixelsPerBeat * (1.0 + wheel.deltaY * 0.8), cursorX());   // clamped to the zoom range
        return;
    }

    const auto horizontal = e.mods.isShiftDown() ? wheel.deltaY : wheel.deltaX;
    const auto vertical = e.mods.isShiftDown() ? 0.0f : wheel.deltaY;

    if (std::abs (horizontal) > 0.0f)
        m_horizontal.setCurrentRangeStart (m_scrollBeats - horizontal * visibleBeats() * 0.5);

    if (std::abs (vertical) > 0.0f && e.position.y > (float) tracksTop())
        m_vertical.setCurrentRangeStart (m_scrollY - (double) vertical * trackHeight * 4.0);
}

//==============================================================================
void ArrangementView::addTrack()
{
    juce::PopupMenu menu;
    menu.addSectionHeader ("Add a track");
    auto add = [this] (std::function<std::unique_ptr<MBoxTrack>()> make, juce::String message)
    {
        int newId = 0;

        if (m_session.editSong ([&] (MBoxSong& s) { return s.addTrack (make(), &newId); }, message).wasOk())
            m_session.selectTrack (newId);
    };

    menu.addItem ("Physical input", [add] { add ([] { return std::make_unique<MBoxPhysicalTrack> (0, "Input"); }, "Physical track added"); });
    menu.addItem ("WAV", [add] { add ([] { return std::make_unique<MBoxAudioTrack> (0, "Audio"); }, "WAV track added"); });
    menu.addItem ("Cue clicks", [add] { add ([] { return std::make_unique<MBoxInfoTrack> (0, "Cues"); }, "Cue click track added"); });
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&m_addTrack));
}

void ArrangementView::removeSelectedTrack()
{
    const auto trackId = m_session.getSelectedTrackId();

    if (m_session.getSelectedTrack() == nullptr)
        return;

    UIHelpers::ask ("Remove track", "Remove " + m_session.getSelectedTrack()->getName() + " from this song?", { "Remove", "Cancel" },
                    [this, trackId] (int choice)
                    {
                        if (choice == 0)
                            m_session.editSong ([trackId] (MBoxSong& s) { return s.removeTrack (trackId); },
                                                "Track " + juce::String (trackId) + " removed");
                    });
}

void ArrangementView::chooseWavFor (int trackId, const Bar& start)
{
    if (! m_session.canEdit())
        return;

    m_fileChooser = std::make_unique<juce::FileChooser> ("Choose a WAV (PCM 16-bit, 44.1 kHz, stereo)",
                                                         juce::File::getSpecialLocation (juce::File::userMusicDirectory), "*.wav");
    m_fileChooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                                [this, trackId, start] (const juce::FileChooser& chooser)
                                {
                                    if (const auto file = chooser.getResult(); file.existsAsFile())
                                        m_session.importWav (trackId, start, file);
                                });
}
