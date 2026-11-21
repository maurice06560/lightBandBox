#pragma once

#include "PlayheadFollow.h"
#include "Session.h"
#include "UIHelpers.h"
#include "../Audio/TempoMap.h"

#include <juce_audio_utils/juce_audio_utils.h>

#include <map>

/** The song editor, in the style of Logic's tracks area:

        [ corner tools ][ ruler with bar numbers, cursor and playhead             ]
        [ Tempo         ][ tempo steps, filled, with a value at every change       ]
        [ Signature     ][ time-signature changes                                  ]
        [ track header  ][ lane: WAV region with waveform, or bankslot regions     ]
        ...

    The horizontal axis is in beats (constant tempo = constant speed), so bars of different
    signatures have different widths, as in Logic.

    Click: select a track / move the edit cursor. Drag a WAV region left or right to move it (smoothly, to the
    tick; hold Shift to snap to beats). Double-click a lane to add an entry there (tempo, signature,
    bankslot switch) or to place a WAV; double-click a track header to rename the track.
    Wheel scrolls; shift-wheel scrolls sideways; cmd/ctrl-wheel zooms. Zoom goes from the whole song
    (fully out) to one bar across the view (fully in), always around the edit cursor.
*/
class ArrangementView final : public juce::Component,
                              private Session::Listener,
                              private juce::Timer,
                              private juce::ChangeListener,
                              private juce::ScrollBar::Listener
{
public:
    explicit ArrangementView (Session& session);
    ~ArrangementView() override;

    static constexpr int headerWidth = 240;
    static constexpr int rulerHeight = 36;
    static constexpr int tempoLaneHeight = 96;
    static constexpr int signatureLaneHeight = 30;
    static constexpr int trackHeight = 68;
    static constexpr int scrollBarSize = 10;

    void paint (juce::Graphics&) override;
    void resized() override;

    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;

private:
    enum class Zone
    {
        None,
        Ruler,
        TempoLane,
        SignatureLane,
        TrackHeader,
        TrackLane
    };

    struct Hit
    {
        Zone zone = Zone::None;
        int trackIndex = -1;
        double beats = 0.0;
    };

    void sessionChanged() override;

    /** Track headers show the faders, mute / solo and monitoring; muted regions are greyed. */

    void sessionMixChanged() override { repaint(); }
    void timerCallback() override;
    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    void scrollBarMoved (juce::ScrollBar*, double newRangeStart) override;

    // Geometry
    juce::Rectangle<int> timelineArea() const;   // right of the headers, left of the vertical scrollbar, above the horizontal one
    int tracksTop() const noexcept { return rulerHeight + tempoLaneHeight + signatureLaneHeight; }
    float xForBeats (double beats) const;
    double beatsForX (float x) const;
    double visibleBeats() const;
    double totalBeats() const;
    juce::Rectangle<int> trackRowBounds (int index) const;
    Hit hitAt (juce::Point<float>) const;
    Bar barAt (double beats, bool snapToBeat) const;
    void updateScrollBars();
    void setZoom (double pixelsPerBeat, float anchorX);
    void setZoomProportion (double proportion, float anchorX);
    double minZoom() const;   // pixels per beat that fit the whole song
    double maxZoom() const;   // pixels per beat that fit one (the longest) bar
    double zoomProportion() const;

    /** The WAV region of a track at a given start position, in this component's coordinates. */
    juce::Rectangle<float> wavRegionBounds (juce::Rectangle<int> lane, const MBoxTrack&, const Bar& start);
    /** The Wav track whose region is under the point, or nullptr. */
    const MBoxTrack* wavRegionAt (juce::Point<float>);

    // Painting
    void paintRuler (juce::Graphics&, juce::Rectangle<int> area);
    void paintTempoLane (juce::Graphics&, juce::Rectangle<int> header, juce::Rectangle<int> lane);
    void paintSignatureLane (juce::Graphics&, juce::Rectangle<int> header, juce::Rectangle<int> lane);
    void paintTracks (juce::Graphics&);
    void paintTrackHeader (juce::Graphics&, juce::Rectangle<int> area, const MBoxTrack&, int index, bool selected);
    void paintTrackLane (juce::Graphics&, juce::Rectangle<int> area, const MBoxTrack&, bool selected);
    void paintWavRegion (juce::Graphics&, juce::Rectangle<int> lane, const MBoxTrack&, bool greyed);
    void paintBankRegions (juce::Graphics&, juce::Rectangle<int> lane, const MBoxTrack&, bool greyed);

    /** The M (mute) or S (solo) button painted in a track header row. */
    /** The track header's buttons, right to left: S, M, and I (input monitoring, physical tracks only). */
    enum class HeaderButton { solo, mute, monitor };
    juce::Rectangle<float> headerButtonBounds (juce::Rectangle<int> row, HeaderButton button) const;
    /** The header button at a position, if any (I only on physical tracks). */
    std::optional<HeaderButton> headerButtonAt (juce::Rectangle<int> row, const MBoxTrack& track, juce::Point<float> position) const;
    void paintHeaderButton (juce::Graphics&, juce::Rectangle<float>, const juce::String& text, bool on, juce::Colour onColour);
    void paintGrid (juce::Graphics&, juce::Rectangle<int> area, bool strong);
    void paintPastEnd (juce::Graphics&, juce::Rectangle<int> area);
    void paintPlayheadAndCursor (juce::Graphics&);

    // Actions
    void addTrack();
    void removeSelectedTrack();
    void chooseWavFor (int trackId, const Bar& start);

    juce::AudioThumbnail* getThumbnail (const juce::File&);
    /** Draws the visible part of a WAV region's waveform from cached image tiles (see m_waveTiles). */
    void paintWaveform (juce::Graphics&, juce::AudioThumbnail&, const MBoxTrack&, juce::Rectangle<float> waveArea,
                        juce::Rectangle<int> lane, double startBeats, double startSeconds, double endBeats, juce::Colour colour);
    double getWavLengthSeconds (const juce::File&);
    juce::File wavFileFor (const MBoxTrack&) const;
    float playheadX() const;
    /** While playing, scrolls so the playhead settles in the middle of the timeline and stays there.
        Returns true when it scrolled (the whole view needs repainting).
    */
    bool followPlayhead();
    /** The playhead in beats while playing past any count-in; -1 otherwise (no region is highlighted then). */
    double activePlayBeats() const;
    /** For each track, the index of the bankslot region the playhead is in (-1 for none / WAV tracks). */
    std::vector<int> activeRegions() const;
    /** Where the edit cursor is drawn: every zoom pivots on it, so it (and its beat) stays put. */
    float cursorX() const;

    Session& m_session;
    std::unique_ptr<TempoMap> m_tempoMap;

    double m_pixelsPerBeat = 14.0;
    juce::String m_zoomedSong;   // the song the zoom was fitted to; a new song starts zoomed out

    struct WavDrag
    {
        int trackId;
        Bar original;
        Bar preview;
        double grabOffsetBeats;   // mouse position minus region start, so the region doesn't jump
    };

    std::optional<WavDrag> m_wavDrag;
    double m_scrollBeats = 0.0;
    int m_scrollY = 0;
    float m_lastPlayheadX = -1.0f;
    PlayheadFollow m_follow;
    double m_lastViewGestureMs = 0.0;   // the user's last scroll or zoom (Time::getMillisecondCounterHiRes)
    std::vector<int> m_lastActiveRegions;   // per track, which bankslot region was drawn highlighted (-1 = none)

    juce::ScrollBar m_horizontal { false };
    juce::ScrollBar m_vertical { true };

    ui::IconButton m_addTrack { ui::IconButton::Icon::Plus };
    ui::IconButton m_removeTrack { ui::IconButton::Icon::Minus };
    ui::Label m_zoomCaption { "Zoom", ui::Label::Style::Caption };
    ui::Fader m_zoom { ui::Fader::Orientation::Horizontal };

    juce::AudioFormatManager m_formats;
    juce::AudioThumbnailCache m_thumbnailCache { 32 };
    std::map<juce::String, std::unique_ptr<juce::AudioThumbnail>> m_thumbnails;

    /** A WAV region's waveform as image tiles, so that following the playhead (a full repaint 30 times
        a second) blits images instead of recomputing the waveform. Tied to what they were drawn for
        (file, zoom, height, colour, start, tempos): anything else, and they are drawn again.
    */
    struct WaveTiles
    {
        static constexpr int width = 256;
        static constexpr size_t maxTiles = 48;
        juce::String key;
        std::map<int, juce::Image> tiles;   // tile index (from the region's start) -> image
    };

    std::map<int, WaveTiles> m_waveTiles;   // per track id

    std::unique_ptr<juce::FileChooser> m_fileChooser;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ArrangementView)
};
