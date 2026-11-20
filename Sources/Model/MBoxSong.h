#pragma once

#include "MBoxTimeline.h"
#include "MBoxTrack.h"

#include <array>

//==============================================================================
// How one track sounds in one mix bus, for one song. Volume is in dB (0.0 is unity gain,
// minVolumeDb or below is silent), balance goes from -1.0 (left) to +1.0 (right).
// Solos add up: when any track of a bus is soloed, only the soloed tracks of that bus play.
//==============================================================================
struct MBoxTrackMix
{
    int m_trackId { 1 };
    double m_volume { 0.0 };
    double m_balance { 0.0 };
    bool m_mute { false };
    bool m_solo { false };
    std::vector<MBoxEffect> m_inserts;
};

//==============================================================================
// A song: its tempo and signature grids, locators, tracks and per-bus mixes.
//
// Invariants kept by the methods:
// - The grids always have an entry at bar 1 and are sorted. Signatures only change at the
//   start of a bar.
// - Every position (grid entries, locators, audio starts, slot changes, info clicks) is valid
//   for the signature of its bar. After a signature change, out-of-range beats and divisions
//   are clamped to the last valid one.
// - m_leftLocator < m_rightLocator, and m_rightLocator.m_bar <= m_totalNbOfBars.
// - When the end of a WAV moves after m_rightLocator, m_rightLocator moves to that end (it
//   never moves earlier on its own).
// - Every bus has exactly one MBoxTrackMix per track, in track order.
//==============================================================================
class MBoxSong
{
public:
    explicit MBoxSong (const juce::String& name);

    // Deep copy, used to give the loader threads a snapshot they can read while the original is edited
    MBoxSong (const MBoxSong& other);
    MBoxSong& operator= (const MBoxSong&) = delete;

    juce::String getName() const;

    void setTotalBar (const int bar);
    int getTotalBar() const;

    // Adds or replaces the tempo at a position. Fails for a tempo outside minTempo..maxTempo
    // or an invalid position.
    juce::Result addTempo (const std::pair<Bar, int>& tempo);
    // Fails for the entry at bar 1, which every song needs
    juce::Result removeTempo (const Bar& tempo);
    const std::vector<std::pair<Bar, int>>& getTempoGrid() const noexcept  { return m_tempoGrid; }

    // Adds or replaces the signature at a bar. The position must be the start of a bar.
    juce::Result addSignature (const std::pair<Bar, Signature>& signature);
    // Fails for the entry at bar 1, which every song needs
    juce::Result removeSignature (const Bar& signature);
    const std::vector<std::pair<Bar, Signature>>& getSignatureGrid() const noexcept  { return m_signatureGrid; }

    // A timeline built from the current grids
    MBoxTimeline getTimeline() const;

    juce::Result setLeftLocator (const Bar& position);
    juce::Result setRightLocator (const Bar& position);
    Bar getLeftLocator() const noexcept   { return m_leftLocator; }
    Bar getRightLocator() const noexcept  { return m_rightLocator; }

    void setEndSongAction (EndSongAction action) noexcept  { m_endSongAction = action; }
    EndSongAction getEndSongAction() const noexcept       { return m_endSongAction; }

    // Seconds of silence after the right locator before the next song (and its count-in), 0 to 30
    void setEndSongDelay (int seconds) noexcept;
    int getEndSongDelay() const noexcept  { return m_endStopDelay; }

    void setMetronomeState (MetronomeState state) noexcept  { m_metronomeState = state; }
    MetronomeState getMetronomeState() const noexcept       { return m_metronomeState; }

    //==========================================================================
    // Tracks

    // Adds a track, giving it the next free id (returned through newId), and adds its default
    // mix to every bus. Fails when the song already has maxNumberOfTrackPerSong tracks.
    juce::Result addTrack (std::unique_ptr<MBoxTrack> track, int* newId = nullptr);
    juce::Result removeTrack (int trackId);
    MBoxTrack* getTrack (int trackId) const;
    const std::vector<std::unique_ptr<MBoxTrack>>& getTracks() const noexcept  { return m_tracks; }

    // Sets an audio track's file, reading its length from the WAV header. Fails, leaving the
    // track unchanged, when the file isn't a 44100 Hz, 16-bit stereo WAV.
    juce::Result setAudioFile (int trackId, const juce::File& file);
    juce::Result setAudioStart (int trackId, const Bar& position);

    // Moves the right locator (and the bar count) to the end of the furthest WAV when that end
    // is after the right locator. Called after every change that can move a WAV's end.
    void extendRightLocatorToAudio();

    //==========================================================================
    // Mixes. Bus numbers are 1-based.

    MBoxTrackMix* getTrackMix (int bus, int trackId);
    const MBoxTrackMix* getTrackMix (int bus, int trackId) const;
    const std::vector<MBoxTrackMix>& getMix (int bus) const;
    std::vector<MBoxTrackMix>& getMix (int bus);

    //==========================================================================
    // Loading from storage: these skip the checks that need a complete song and are only
    // used by MBoxStorage, which calls validateAfterLoad() at the end.
    void setNameForStorage (const juce::String& name)  { m_name = name; }
    void setGridsForStorage (std::vector<std::pair<Bar, int>> tempoGrid, std::vector<std::pair<Bar, Signature>> signatureGrid);
    void setLocatorsForStorage (const Bar& left, const Bar& right)  { m_leftLocator = left; m_rightLocator = right; }
    void addTrackForStorage (std::unique_ptr<MBoxTrack> track);
    juce::Result validateAfterLoad();

private:
    friend class MBoxPlaylist;  // renames

    void sortAndCompleteGrids();
    void clampPositionsToSignatures();
    Bar clampPosition (const MBoxTimeline& timeline, Bar position) const;
    void ensureMixesMatchTracks();

    juce::String m_name;
    std::vector<std::pair<Bar, int>> m_tempoGrid;
    std::vector<std::pair<Bar, Signature>> m_signatureGrid;
    int m_totalNbOfBars { defaultTotalBar };
    Bar m_leftLocator { 1, 1, 1, 0 };
    Bar m_rightLocator { defaultTotalBar, 1, 1, 0 };
    std::vector<std::unique_ptr<MBoxTrack>> m_tracks;
    EndSongAction m_endSongAction { EndSongAction::Stop };
    int m_endStopDelay { 0 };
    MetronomeState m_metronomeState { MetronomeState::Off };
    std::array<std::vector<MBoxTrackMix>, numberOfMixBus> m_mixes;
};
