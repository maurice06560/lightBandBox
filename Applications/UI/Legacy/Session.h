#pragma once

#include "../MBoxApplication.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <map>
#include <set>

/** The UI's view of the application: the current selection, unsaved changes and status messages,
    plus the rules every panel follows. All panels edit through it so that it can:

    - refuse edits while a song plays (track volume excepted),
    - remember which songs and banks have unsaved changes (following renames),
    - report the outcome of every action in the status bar,
    - notify every panel to refresh after a change,
    - keep the selected song loaded in the engine while stopped (MBoxApplication::preparePlayback),
      shortly after each change, so that Play starts at once.

    Message thread only.
*/
class Session : private juce::Timer
{
public:
    enum class ListTab
    {
        Song,
        Track,
        Tempo,
        Signature,
        Bankslots
    };

    struct Listener
    {
        virtual ~Listener() = default;

        /** Something changed: selection, structure, content, status or transport. Refresh from the model. */
        virtual void sessionChanged() = 0;

        /** Only live mixing values changed (faders, mute/solo, monitoring, the click's on/off, volume
            and tone) and maybe the status: the selection, structure and content are as they were.
            They change many times a second while dragging, so panels that don't show them can ignore it.
        */
        virtual void sessionMixChanged() { sessionChanged(); }

        /** A panel asks the list editor to show a tab and select the entry at a position. */
        virtual void showInList (ListTab, const Bar&) {}
    };

    explicit Session (MBoxApplication& app);

    MBoxApplication& getApp() noexcept { return m_app; }

    void addListener (Listener* listener)     { m_listeners.add (listener); }
    void removeListener (Listener* listener)  { m_listeners.remove (listener); }

    /** Notifies every listener. */
    void changed();
    void showInList (ListTab tab, const Bar& position);

    //==============================================================================
    // Selection
    const juce::String& getSelectedPlaylistName() const noexcept { return m_playlist; }
    const juce::String& getSelectedSongName() const noexcept     { return m_song; }
    int getSelectedTrackId() const noexcept                       { return m_trackId; }

    MBoxPlaylist* getSelectedPlaylist();
    MBoxSong* getSelectedSong();
    MBoxTrack* getSelectedTrack();

    void selectPlaylist (const juce::String& playlist);
    void selectSong (const juce::String& playlist, const juce::String& song);
    void selectTrack (int trackId);
    /** Selects the first playlist and song available (used at startup and after a load). */
    void selectFirstSong();
    /** What selectFirstSong() selects: (playlist, song), the song empty when no playlist has one. */
    std::pair<juce::String, juce::String> findFirstSong() const;

    /** The mix the mixer shows and edits (0-based). Each playlist keeps its own: switching songs
        within a playlist keeps the mix, and a playlist not shown yet starts on Mix 1.
        Track volume, mute / solo and inserts edits apply to it.
    */
    int getSelectedMixIndex() const noexcept                      { return m_mix; }
    MBoxSubmix* getSelectedMix();
    void selectMix (int mix);

    /** The edit cursor: where "+" buttons add tempo, signature and bankslot entries. */
    const Bar& getCursor() const noexcept { return m_cursor; }
    void setCursor (const Bar& position);
    /** The selected song's start bar, where the cursor goes when a song is selected (bar 1 without a song). */
    Bar songStartCursor();

    //==============================================================================
    // Status bar
    const juce::String& getStatus() const noexcept { return m_status; }
    bool isStatusError() const noexcept             { return m_statusIsError; }
    void setStatus (const juce::String& message, bool isError = false);

    /** Shows the error, or the success message, and returns result.wasOk(). */
    bool report (const juce::Result& result, const juce::String& successMessage = {});

    //==============================================================================
    // Editing
    /** False (with a status message) while a song is playing or paused. */
    bool canEdit();

    /** True while a song is playing or paused: panels grey out every edit except the live ones
        (track volume and the metronome).
    */
    bool isEditLocked() const;

    /** For --snapshot --locked: renders the UI as it looks while a song plays or is paused. */
    void setPreviewLocked (bool locked) { m_previewLocked = locked; }

    /** Applies an edit to the selected song: checks canEdit(), runs it, reports the result, marks the
        song unsaved on success and notifies listeners. Returns the edit's result.
    */
    juce::Result editSong (const std::function<juce::Result (MBoxSong&)>& edit, const juce::String& successMessage = {});

    /** Same, for the selected track of the selected song. */
    juce::Result editTrack (const std::function<juce::Result (MBoxTrack&)>& edit, const juce::String& successMessage = {});

    /** Same, for the banks (marks the banks unsaved). */
    juce::Result editBanks (const std::function<juce::Result (MBoxApplication::Banks&)>& edit, const juce::String& successMessage = {});
    /** For changes made outside editBanks: a plugin's parameters edited in its own window. Marks the
        banks unsaved and notifies the listeners only on the first change, so dragging is cheap.
    */
    void markBanksChanged();
    /** Records a bankslot's measured loudness (nullopt: reset). Allowed while playing, like a meter;
        marks the banks unsaved, notifying like markBanksChanged() (the Banks window repaints itself).
    */
    void setSlotLoudness (BankSlotRef ref, std::optional<float> lufs);

    /** The selected mix's latency (see MBoxAudioEngine::computeLatency), or nullopt while unknown
        (no audio device, or the song isn't loaded yet). Calculated once the selected song is loaded,
        then again only after a strip plugin is added or removed, or a physical track's bankslots change.
    */
    std::optional<MixLatency> getSelectedMixLatency() const;

    /** Renames one of the mixes in every song of the selected playlist (they all share their mix
        names), marking them all unsaved.
    */
    juce::Result renameMix (int mix, const juce::String& name);
    /** Sets a mix's master outputs (MBoxMaster::noOutputs for N/A). Another mix using one of those
        channels is set to N/A, and the status says so. outputsText describes them for the status.
    */
    juce::Result setMixOutputs (int mix, MBoxTrack::Outputs outputs, const juce::String& outputsText);

    /** A track's DI send (MBoxTrack::noDiSend for none); refused on an output a mix uses, and during
        playback (like other track edits).
    */
    juce::Result setTrackDiSend (int trackId, MBoxTrack::Outputs outputs, const juce::String& outputsText);

    /** The mixer strip id of the master in editInserts() / getInserts(); tracks use their id. */
    static constexpr int masterStrip = -1;
    /** The selected mix's inserts of a track (by id) or of its master; nullptr if there is no such strip. */
    MBoxInserts* getInserts (int stripId);
    /** Same as editSong, for one strip's inserts. */
    juce::Result editInserts (int stripId, const std::function<juce::Result (MBoxInserts&)>& edit, const juce::String& successMessage = {});
    /** For an insert's settings changed in its plugin window (allowed during playback: heard live).
        Marks the selected song unsaved; insert windows only stay open for the selected song.
    */
    void markSongChanged();

    /** Track volume (in the selected mix) is allowed during playback; the engine ramps to it live. */
    juce::Result setTrackVolume (int trackId, float decibels);
    /** The selected mix's master volume; allowed at any time and heard live, like track volume. */
    juce::Result setMasterVolume (float decibels);
    /** The selected mix's master balance (-64 to +63); allowed at any time and heard live. */
    juce::Result setMasterBalance (int balance);

    /** Mute / solo in the selected mix (exclusive per track), allowed at any time and heard live. */
    juce::Result setTrackMute (int trackId, bool mute);
    juce::Result setTrackSolo (int trackId, bool solo);

    /** Balance in the selected mix (-64 to +63), allowed at any time and heard live. */
    juce::Result setTrackBalance (int trackId, int balance);

    /** Whether a physical track's input is heard while the song is stopped or paused (through the
        bankslot at the edit cursor). Allowed at any time, heard live; marks the song unsaved.
    */
    juce::Result setTrackMonitored (int trackId, bool monitor);

    /** Metronome settings of the selected song, allowed at any time. On/off, volume and tone are
        heard at once if the song is playing or paused; the countdown applies from the next play.
        (The mix it plays in is a song edit: editSong.)

        On/off, volume and tone are practice controls, not song changes: they don't mark the song
        unsaved (they are still written whenever the song is saved). Pass SongChange::yes for
        settings that belong to the song's setup (countdown).
    */
    enum class SongChange { no, yes };

    /** Whether the selected song's click plays in a mix (any combination, none included). Allowed at
        any time and heard live; marks the song unsaved.
    */
    juce::Result setClickInMix (int mix, bool on);
    juce::Result editMetronome (const std::function<juce::Result (MBoxSong&)>& edit, const juce::String& successMessage = {},
                                SongChange songChange = SongChange::no);

    //==============================================================================
    // Structure: these write to disk at once, like the backend does.
    juce::Result createPlaylist (const juce::String& name);
    juce::Result renamePlaylist (const juce::String& oldName, const juce::String& newName);
    juce::Result deletePlaylist (const juce::String& name);

    /** Copy / paste a playlist. Copy remembers which one (it follows a rename, a delete forgets it);
        paste makes a deep copy of it as it is then (MBoxApplication::copyPlaylist), named "<name> copy",
        and selects it. Refused during playback.
    */
    void copyPlaylist (const juce::String& name);
    bool canPastePlaylist() const;
    juce::Result pastePlaylist();
    juce::Result createSong (const juce::String& playlist, const juce::String& name);
    juce::Result renameSong (const juce::String& playlist, const juce::String& oldName, const juce::String& newName);
    juce::Result deleteSong (const juce::String& playlist, const juce::String& name);
    juce::Result importWav (int trackId, const Bar& start, const juce::File& file);
    juce::Result moveWav (int trackId, const Bar& start);
    juce::Result loadFolder (const juce::File& folder);

    //==============================================================================
    // Unsaved changes
    bool isSongDirty (const juce::String& playlist, const juce::String& song) const;
    bool isPlaylistDirty (const juce::String& playlist) const;
    bool areBanksDirty() const noexcept { return m_banksDirty; }
    bool hasUnsavedChanges() const      { return m_banksDirty || ! m_dirtySongs.empty(); }
    void markSongDirty (const juce::String& playlist, const juce::String& song);

    juce::Result saveSelectedSong();
    juce::Result savePlaylist (const juce::String& playlist);
    /** Saves every playlist with unsaved songs, and the banks. */
    juce::Result saveAll();

    //==============================================================================
    // Transport
    /** From stopped, plays from the edit cursor; from paused, resumes where it paused. */
    juce::Result play();
    /** Always plays from the edit cursor: from stopped like play(); while playing or paused it stops,
        lets the stop's fade-out finish, then plays from the cursor (Ctrl + Space).
    */
    juce::Result playFromCursor();

    /** The playing song reached its end: stops, then, if the song says so and it isn't the last of its
        playlist, selects the next song and plays it from the start after the song's delay. Playing,
        stopping or selecting anything meanwhile cancels the automatic start.
    */
    void songReachedEnd();
    /** True while waiting to start the next song automatically. */
    bool isWaitingForNextSong() const noexcept { return m_waitingForNext; }

    /** What a song does at its end. Allowed at any time, even during playback (it applies to the
        current play); marks the song unsaved.
    */
    juce::Result setSongEnd (const juce::String& playlist, const juce::String& song, MBoxSong::EndAction action, int delay);
    void pause();
    void stop();
    bool isPlaying() const;

    /** Called regularly (by the transport bar's timer): notices a device restart (another device,
        sample rate or buffer size, even changed outside the app) and plugins whose latency changed,
        and recalculates the latency (and reloads the song) then.
    */
    void pollEngine();

private:
    void timerCallback() override;
    /** After a live mixing change: notifies sessionMixChanged(), or everything if the song has just
        become unsaved (its title and the browser show that). Doesn't reload the song: the engine has it.
    */
    void liveChanged (bool songBecameDirty);
    /** Like report(), without notifying (the caller does). */
    void reportQuietly (const juce::Result& result, const juce::String& successMessage = {});
    /** Starts loading, in the background, the insert plugins of the song after the selected one. */
    void preloadNextSong();
    static juce::String songKey (const juce::String& playlist, const juce::String& song) { return playlist + "\n" + song; }
    void clearDirtyPlaylist (const juce::String& playlist);
    void validateSelection();
    /** Before the selection moves to another song: insert windows belong to the selected song. */
    void closeInsertWindows();
    /** The mix last selected in a playlist (0 if none yet). */
    int rememberedMix (const juce::String& playlist) const;
    /** Recalculates the latencies if they are stale and the engine has the song loaded. */
    void refreshLatencyIfStale();
    void invalidateLatency() noexcept { m_latencyStale = true; m_latencies.reset(); }
    /** The physical tracks and their bankslot lists: when it changes, the latency is recalculated. */
    static juce::String physicalBankslotsKey (const MBoxSong& song);
    void cancelAutoNext() noexcept { ++m_autoNextToken; m_waitingForNext = false; }

    MBoxApplication& m_app;
    juce::ListenerList<Listener> m_listeners;

    juce::String m_playlist, m_song;
    int m_trackId { 0 };
    int m_mix { 0 };
    std::map<juce::String, int> m_mixByPlaylist;
    juce::String m_copiedPlaylist;   // what pastePlaylist() copies
    std::optional<std::array<MixLatency, MBoxSong::numMixes>> m_latencies;
    bool m_latencyStale { true };
    Bar m_cursor;

    juce::String m_status;
    bool m_statusIsError { false };

    bool m_previewLocked { false };
    std::set<juce::String> m_dirtySongs;
    bool m_banksDirty { false };
    int m_autoNextToken { 0 };
    bool m_waitingForNext { false };
    std::shared_ptr<bool> m_alive { std::make_shared<bool> (true) };   // for delayed callbacks that may outlive this
    int m_deviceGeneration { -1 };
    int m_pollCount { 0 };

    /** Instantiates one insert plugin of the next song per tick, so selecting it (or the automatic
        "play next") doesn't have to.
    */
    struct Preloader final : juce::Timer
    {
        explicit Preloader (Session& s) : session (s) {}
        void timerCallback() override;

        Session& session;
        juce::String playlist, song;
    };

    Preloader m_preloader { *this };
};
