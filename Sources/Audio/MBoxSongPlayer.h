#pragma once

#include "MBoxAudioClipCache.h"
#include "MBoxClickSynth.h"
#include "../Model/MBoxPlaylist.h"
#include "../Model/MBoxRig.h"
#include "../Plugins/MBoxLiveInput.h"

#include <array>
#include <atomic>
#include <optional>

class MBoxPluginHost;

//==============================================================================
// The runtime state of one track in one mix bus. The message thread writes the targets; the
// audio thread reads them and moves its current values towards them, so changes are smooth.
//==============================================================================
struct MBoxMixState
{
    std::atomic<float> m_gain { 1.0f };      // linear, from the volume in dB
    std::atomic<float> m_balance { 0.0f };
    std::atomic<bool> m_mute { false };
    std::atomic<bool> m_solo { false };

    // The highest sample level (linear) this track sent to the bus since the last takePeak()
    std::atomic<float> m_peak { 0.0f };
    float takePeak() noexcept  { return m_peak.exchange (0.0f); }

    // Audio thread only
    float m_currentGain { 1.0f };
    float m_currentBalance { 0.0f };

    void setFrom (const MBoxTrackMix& trackMix) noexcept;
};

//==============================================================================
// Everything the audio thread needs to play one song, built from a snapshot of the model by
// buildPlaylistPlayer(). All positions are frames from the start of bar 1 (44100 Hz).
// Apart from the mix states, nothing in it changes once it's handed to the audio thread.
//==============================================================================
struct MBoxSongPlayer
{
    struct Track
    {
        int m_trackId { 0 };
        TrackType m_type { TrackType::Audio };

        // Audio tracks
        std::shared_ptr<const MBoxAudioClip> m_clip;
        std::int64_t m_startFrame { 0 };

        // Physical tracks
        int m_physicalInput { -1 };
        int m_diOutput { -1 };
        MBoxLiveInput* m_liveInput { nullptr };                          // owned by the playlist player
        std::vector<std::pair<std::int64_t, MBoxSlotRef>> m_slotChanges;  // sorted by frame

        // Info tracks
        std::vector<MBoxClickEvent> m_infoClicks;

        // Per mix bus: inserts (always present, possibly empty, padded for latency) and mix state
        std::array<std::unique_ptr<MBoxEffectChain>, numberOfMixBus> m_inserts;
        std::array<std::unique_ptr<MBoxMixState>, numberOfMixBus> m_mix;

        // The slot of a physical track at a frame: the last change at or before it, or the
        // first change when the frame is before all of them
        std::optional<MBoxSlotRef> getSlotAt (std::int64_t frame) const noexcept;
    };

    juce::String m_name;
    MBoxTimeline m_timeline;
    std::int64_t m_leftFrame { 0 };
    std::int64_t m_rightFrame { 0 };
    std::int64_t m_preRollFrames { 0 };   // count-in length, 0 when the metronome is Off
    std::int64_t m_endDelayFrames { 0 };  // silence after the right locator before the next song
    EndSongAction m_endSongAction { EndSongAction::Stop };
    std::vector<MBoxClickEvent> m_metronomeClicks;  // count-in, then every beat-unit when On
    std::vector<Track> m_tracks;

    Track* findTrack (int trackId) noexcept;
};

//==============================================================================
// The runtime version of a whole playlist, swapped in as one unit by the renderer.
//==============================================================================
struct MBoxPlaylistPlayer
{
    juce::String m_playlistName;
    std::vector<std::unique_ptr<MBoxSongPlayer>> m_songs;
    std::vector<std::unique_ptr<MBoxLiveInput>> m_liveInputs;
    std::array<std::unique_ptr<MBoxEffectChain>, numberOfMixBus> m_masterChains;
    int m_totalLatency { 0 };       // every path (live, WAV, clicks) is delayed to this
    std::uint64_t m_generation { 0 };  // set by the renderer when published
};

//==============================================================================
// What buildPlaylistPlayer needs besides the playlist
//==============================================================================
struct MBoxPlayerBuildContext
{
    MBoxAudioClipCache& m_clips;
    MBoxPluginHost* m_pluginHost { nullptr };  // may be null when no song uses plugins (tests)
    const MBoxBanks& m_banks;
    const MBoxRig& m_rig;
    double m_sampleRate { supportedSampleRate };
    int m_maxBlockSize { 256 };
    double m_warmUpSeconds { 2.0 };
    std::function<bool()> m_shouldCancel;          // checked between steps; true aborts the build
    std::function<void (float)> m_onProgress;      // 0 to 1
};

// Builds the players of every song of a playlist snapshot: decodes the WAVs (through the
// cache), computes every click, creates, prepares and warms up every plugin chain, and pads
// all paths to a common latency. Slow; call it on a loader thread with a copy of the
// playlist. Returns nullptr with an error on failure or cancellation.
std::unique_ptr<MBoxPlaylistPlayer> buildPlaylistPlayer (const MBoxPlaylist& playlist,
                                                         const MBoxPlayerBuildContext& context,
                                                         juce::String& error);
