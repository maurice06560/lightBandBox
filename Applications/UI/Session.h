#pragma once

#include <MBoxCoreAudio.h>
#include <Model/MBoxRig.h>
#include <Model/MBoxSong.h>

#include <juce_events/juce_events.h>

#include <span>

//==============================================================================
// The UI's view of MBoxCoreAudio: the current selection, the status message, and the rules
// every panel follows. All panels edit through it so that it can:
//
// - refuse edits while a song plays or is paused (live mixing and the song-end and metronome
//   settings excepted),
// - tell the library about every model edit (songChanged / rigChanged / banksChanged), which
//   saves it about a second later and rebuilds the loaded playlist in the background,
// - report the outcome of every action in the status bar,
// - notify every panel after a change, including changes made through the library's events
//   (a TCP client, the playlist moving on to the next song, loading progress).
//
// Selecting a song selects it in the library too, which loads its playlist in the background
// when it isn't loaded yet. Live mixing (track and master volume, balance, mute, solo, the
// metronome volume) acts on the library's loaded song, so it waits for that load to finish.
//
// Mix buses are 1-based, like everywhere in the library. Message thread only.
//==============================================================================
class Session final
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

        // Something changed: selection, structure, content, status or transport state.
        // Refresh from the model.
        virtual void sessionChanged() = 0;

        // Only live mixing values changed (faders, balance, mute, solo, master and metronome
        // volume), from this UI or a TCP client. They change many times a second while dragging.
        virtual void sessionMixChanged() { sessionChanged(); }

        // The playhead moved (about every 50 ms while playing). See getTransport().
        virtual void sessionTransportChanged() {}

        // The library replaced its running playlist (a load, a rebuild after an edit, or an
        // unload): the plugin instances from getRunningPlugins() are gone. Called before they
        // are freed, so their editors can close.
        virtual void sessionPlayerReplaced() {}

        // Selecting a song of another playlist started loading it. Called before the panels
        // refresh for the new song, so a progress display can show at once.
        virtual void sessionLoadStarted() {}

        // A panel asks the list editor to show a tab and select the entry at a position
        virtual void showInList (ListTab, const Bar&) {}
    };

    explicit Session (MBoxCoreAudio& core);
    ~Session();

    MBoxCoreAudio& getCore() noexcept  { return m_core; }

    void addListener (Listener* listener)     { m_listeners.add (listener); }
    void removeListener (Listener* listener)  { m_listeners.remove (listener); }

    // Checks the selection still exists, then notifies every listener
    void changed();
    void showInList (ListTab tab, const Bar& position);

    //==========================================================================
    // Playlists and songs, in their order
    juce::StringArray getPlaylistNames() const;
    juce::StringArray getSongNames (const juce::String& playlist) const;

    //==========================================================================
    // Selection
    const juce::String& getSelectedPlaylistName() const noexcept  { return m_playlist; }
    const juce::String& getSelectedSongName() const noexcept      { return m_song; }
    // 0 when the selected song has no tracks
    int getSelectedTrackId() const noexcept                       { return m_trackId; }

    MBoxSong* getSelectedSong();
    MBoxTrack* getSelectedTrack();

    // Selecting a playlist clears the song selection (its first song isn't loaded)
    void selectPlaylist (const juce::String& playlist);
    // Stops playback and selects the song in the library, loading its playlist if needed
    void selectSong (const juce::String& playlist, const juce::String& song);
    void selectTrack (int trackId);
    // Selects the song the library has loaded, or else the first song of the first playlist
    // that has one. Called at startup.
    void selectFirstSong();
    // (playlist, song) of the first playlist with a song; the song is empty if none has one
    std::pair<juce::String, juce::String> findFirstSong() const;

    // True when the library has the selected song loaded, so live mixing reaches it
    bool isSelectionLoaded() const;

    //==========================================================================
    // The mix bus the mixer shows and edits (1 to numberOfMixBus). Rack-wide, like the buses.
    int getSelectedBus() const noexcept  { return m_bus; }
    void selectBus (int bus);
    MBoxMixBus& getSelectedMixBus();
    // The selected song's mix of a track in the selected bus, or nullptr
    MBoxTrackMix* getTrackMix (int trackId);

    //==========================================================================
    // The edit cursor: where "+" buttons add tempo, signature and bankslot entries
    const Bar& getCursor() const noexcept  { return m_cursor; }
    void setCursor (const Bar& position);
    // The selected song's left locator, where the cursor goes when a song is selected
    Bar songStartCursor();

    //==========================================================================
    // Status bar
    const juce::String& getStatus() const noexcept  { return m_status; }
    bool isStatusError() const noexcept              { return m_statusIsError; }
    void setStatus (const juce::String& message, bool isError = false);

    // Shows the error, or the success message (if any), and returns whether it worked
    bool report (const juce::Result& result, const juce::String& successMessage = {});

    //==========================================================================
    // Editing
    // False (with a status message) while a song is playing or paused
    bool canEdit();
    // True while a song is playing or paused: panels grey out every edit except the live ones
    bool isEditLocked() const;
    // For snapshots: renders the UI as it looks while a song plays
    void setPreviewLocked (bool locked)  { m_previewLocked = locked; }

    // Applies an edit to the selected song: checks canEdit(), runs it, tells the library on
    // success (songChanged), reports the result and notifies the listeners.
    juce::Result editSong (const std::function<juce::Result (MBoxSong&)>& edit, const juce::String& successMessage = {});
    // Same, for the selected track of the selected song
    juce::Result editTrack (const std::function<juce::Result (MBoxTrack&)>& edit, const juce::String& successMessage = {});
    // Same, for the rig (bus names, outputs, master chains, device settings): rigChanged
    juce::Result editRig (const std::function<juce::Result (MBoxRig&)>& edit, const juce::String& successMessage = {});
    // Same, for the banks: banksChanged
    juce::Result editBanks (const std::function<juce::Result (std::span<MBoxBank>)>& edit, const juce::String& successMessage = {});

    MBoxRig& getRig();
    std::span<MBoxBank> getBanks();

    // The mixer strip id of the bus master in getInserts() / editInserts(); tracks use their id
    static constexpr int masterStrip = -1;
    // The selected bus's inserts of a track of the selected song, or its master chain; nullptr
    // if there is no such strip
    std::vector<MBoxEffect>* getInserts (int stripId);
    // Same as editSong (a track) or editRig (the master), for one strip's chain
    juce::Result editInserts (int stripId, const std::function<juce::Result (std::vector<MBoxEffect>&)>& edit,
                              const juce::String& successMessage = {});

    //==========================================================================
    // Live mixing, in the selected bus. Allowed at any time and heard at once; they need the
    // selected song to be loaded (isSelectionLoaded). Volumes in dB, balances from -1 to +1.
    juce::Result setTrackVolume (int trackId, double volumeDb);
    juce::Result setTrackBalance (int trackId, double balance);
    juce::Result setTrackMute (int trackId, bool mute);
    juce::Result setTrackSolo (int trackId, bool solo);
    juce::Result setMasterVolume (double volumeDb);
    juce::Result setMasterBalance (double balance);
    juce::Result setMetronomeVolume (double volumeDb);
    // Whether the click plays in the selected bus at all (rack-wide)
    juce::Result setMetronomeInMix (bool on);

    // The selected song's metronome. Allowed at any time, and applied live (no rebuild).
    juce::Result setMetronomeState (MetronomeState state);

    // What a song does at its right locator. Allowed at any time, even during playback.
    juce::Result setSongEnd (const juce::String& playlist, const juce::String& song, EndSongAction action, int delaySeconds);

    //==========================================================================
    // Structure. The library renames and trashes folders at once.
    juce::Result createPlaylist (const juce::String& name);
    juce::Result renamePlaylist (const juce::String& oldName, const juce::String& newName);
    juce::Result deletePlaylist (const juce::String& name);

    // Copy / paste a playlist. Copy remembers which one (it follows a rename, a delete forgets
    // it); paste makes an independent copy of it as it is then, named "<name> copy", and
    // selects it. Refused during playback.
    void copyPlaylist (const juce::String& name);
    bool canPastePlaylist() const;
    juce::Result pastePlaylist();
    juce::Result createSong (const juce::String& playlist, const juce::String& name);
    juce::Result renameSong (const juce::String& playlist, const juce::String& oldName, const juce::String& newName);
    juce::Result deleteSong (const juce::String& playlist, const juce::String& name);

    // Copies a WAV into the selected song's media folder, makes it an audio track's file and
    // places it at start. The right locator moves to the WAV's end if that is later.
    juce::Result importWav (int trackId, const Bar& start, const juce::File& file);
    juce::Result moveWav (int trackId, const Bar& start);

    // Writes the data folder now (it is also saved about a second after every change)
    juce::Result saveAll();

    //==========================================================================
    // Transport. play() resumes after a pause, and starts from the edit cursor after a stop
    // (with the count-in when the cursor is on the left locator).
    juce::Result play();
    // Plays from the edit cursor whether stopped, playing or paused (Ctrl-Space)
    juce::Result playFromCursor();
    void pause();
    void stop();
    bool isPlaying() const;
    // The library's transport as of its last event: state, loaded song, position, tempo, loading
    const MBoxTransportInfo& getTransport() const noexcept  { return m_transport; }
    // While the selected song plays or is paused: the playhead, in frames from the start of
    // bar 1 (before the left locator during the count-in). Read live from the renderer, so it
    // moves smoothly between transport events.
    std::optional<std::int64_t> getPlayheadFrames() const;

    // The data folder the library uses (ONE_BOX_DATA_DIR, or <user home>/OneBox)
    juce::File getDataDirectory() const;

private:
    void handleEvent (const MBoxEvent& event);
    // Reports the error and returns it
    juce::Result fail (const juce::String& message);
    // Like report(), without notifying (the caller does)
    void reportQuietly (const juce::Result& result, const juce::String& successMessage = {});
    juce::Result liveMix (const MBoxResult& result);
    void validateSelection();

    MBoxCoreAudio& m_core;
    juce::ListenerList<Listener> m_listeners;
    int m_eventListenerId { -1 };

    juce::String m_playlist, m_song;
    int m_trackId { 0 };
    int m_bus { 1 };
    Bar m_cursor;

    // The (playlist, song) this UI asked the library to load: until it arrives, songChanged
    // events about another song (the previous playlist moving on) don't move the selection.
    juce::String m_pendingPlaylist, m_pendingSong;
    juce::String m_copiedPlaylist;  // what pastePlaylist() copies

    MBoxTransportInfo m_transport;

    juce::String m_status;
    bool m_statusIsError { false };
    bool m_previewLocked { false };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Session)
};
