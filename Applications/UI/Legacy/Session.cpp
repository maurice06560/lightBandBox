#include "Session.h"
#include "../Audio/MBoxAudioEngine.h"
#include "../Effects/MAudioEffect.h"

Session::Session (MBoxApplication& app)
    : m_app (app)
{
}

void Session::changed()
{
    validateSelection();
    m_listeners.call ([] (Listener& l) { l.sessionChanged(); });

    // Load the song as it now is in the background, so Play doesn't have to.
    if (! isEditLocked() && m_song.isNotEmpty())
        startTimer (400);
}

void Session::timerCallback()
{
    // Not in the middle of a drag (a fader, a region...): the load could stall it.
    if (juce::ModifierKeys::currentModifiers.isAnyMouseButtonDown())
        return;

    stopTimer();

    if (! isEditLocked() && m_song.isNotEmpty())
    {
        if (auto* engine = m_app.getAudioEngine())
            engine->setMonitorPosition (m_cursor);   // e.g. reset to bar 1 by selecting another song

        m_app.preparePlayback (m_playlist, m_song);   // errors (e.g. a missing WAV) are reported by play()
        refreshLatencyIfStale();
        preloadNextSong();
    }
}

void Session::liveChanged (bool songBecameDirty)
{
    if (songBecameDirty)
    {
        validateSelection();
        m_listeners.call ([] (Listener& l) { l.sessionChanged(); });
        return;
    }

    m_listeners.call ([] (Listener& l) { l.sessionMixChanged(); });
}

void Session::reportQuietly (const juce::Result& result, const juce::String& successMessage)
{
    if (result.failed())
    {
        m_status = result.getErrorMessage();
        m_statusIsError = true;
    }
    else if (successMessage.isNotEmpty())
    {
        m_status = successMessage;
        m_statusIsError = false;
    }
}

void Session::preloadNextSong()
{
    auto* playlist = getSelectedPlaylist();

    if (playlist == nullptr)
        return;

    const auto& songs = playlist->getSongs();
    const auto current = std::find_if (songs.begin(), songs.end(), [&] (const MBoxSong& s) { return s.getName() == m_song; });

    if (current == songs.end() || std::next (current) == songs.end())
        return;

    m_preloader.playlist = m_playlist;
    m_preloader.song = std::next (current)->getName();
    m_preloader.startTimer (150);
}

void Session::Preloader::timerCallback()
{
    // One plugin per tick keeps the UI responsive; it stops once the song has none left to load.
    if (! session.m_app.preloadOneInsert (playlist, song))
        stopTimer();
}

void Session::pollEngine()
{
    auto* engine = m_app.getAudioEngine();

    if (engine == nullptr)
        return;

    // A restarted device (another one, or another rate or buffer size): new latency, and maybe a reload.
    if (const auto generation = engine->getDeviceGeneration(); generation != m_deviceGeneration)
    {
        const auto first = m_deviceGeneration < 0;
        m_deviceGeneration = generation;

        if (! first)
        {
            invalidateLatency();
            changed();   // reloads the song (if stopped) and calculates the latency again
        }
    }

    // A plugin's latency may change with its settings: a few times a second is plenty to notice it.
    if (++m_pollCount % 10 == 0 && engine->refreshPluginLatencies())
    {
        m_latencyStale = true;
        refreshLatencyIfStale();
    }
}

void Session::showInList (ListTab tab, const Bar& position)
{
    m_listeners.call ([&] (Listener& l) { l.showInList (tab, position); });
}

//==============================================================================
MBoxPlaylist* Session::getSelectedPlaylist()
{
    return m_app.getPlaylist (m_playlist);
}

MBoxSong* Session::getSelectedSong()
{
    auto* playlist = getSelectedPlaylist();
    return playlist != nullptr ? playlist->getSong (m_song) : nullptr;
}

MBoxTrack* Session::getSelectedTrack()
{
    auto* song = getSelectedSong();
    return song != nullptr ? song->getTrack (m_trackId) : nullptr;
}

void Session::selectPlaylist (const juce::String& playlist)
{
    if (playlist == m_playlist)
        return;

    stop();
    cancelAutoNext();
    closeInsertWindows();
    m_playlist = playlist;
    m_song = {};
    invalidateLatency();
    m_mix = rememberedMix (playlist);
    m_trackId = 0;
    changed();
}

void Session::selectSong (const juce::String& playlist, const juce::String& song)
{
    if (playlist == m_playlist && song == m_song)
        return;

    stop();
    cancelAutoNext();
    closeInsertWindows();
    m_playlist = playlist;
    m_song = song;
    invalidateLatency();   // calculated again once the song is loaded
    m_mix = rememberedMix (playlist);   // the same mix as the playlist's other songs
    m_trackId = 0;
    m_cursor = songStartCursor();
    changed();
}

void Session::selectTrack (int trackId)
{
    if (trackId == m_trackId)
        return;

    m_trackId = trackId;
    changed();
}

void Session::selectFirstSong()
{
    closeInsertWindows();
    m_mixByPlaylist.clear();
    invalidateLatency();
    m_mix = 0;

    std::tie (m_playlist, m_song) = findFirstSong();
    m_trackId = 0;
    changed();
}

std::pair<juce::String, juce::String> Session::findFirstSong() const
{
    for (const auto& playlist : m_app.getPlaylists())
        if (! playlist.getSongs().empty())
            return { playlist.getName(), playlist.getSongs().front().getName() };

    const auto& playlists = m_app.getPlaylists();
    return { playlists.empty() ? juce::String() : playlists.front().getName(), {} };
}

int Session::rememberedMix (const juce::String& playlist) const
{
    const auto it = m_mixByPlaylist.find (playlist);
    return it != m_mixByPlaylist.end() ? it->second : 0;
}

MBoxSubmix* Session::getSelectedMix()
{
    auto* song = getSelectedSong();
    return song != nullptr ? &song->getMix (m_mix) : nullptr;
}

void Session::selectMix (int mix)
{
    if (mix == m_mix || mix < 0 || mix >= MBoxSong::numMixes)
        return;

    m_mix = mix;

    if (m_playlist.isNotEmpty())
        m_mixByPlaylist[m_playlist] = mix;

    if (auto* submix = getSelectedMix())
        setStatus ("Showing " + submix->getName());
    else
        changed();
}

juce::Result Session::renameMix (int mix, const juce::String& name)
{
    if (mix < 0 || mix >= MBoxSong::numMixes)
        return juce::Result::fail ("No such mix");

    auto* playlist = getSelectedPlaylist();

    if (playlist == nullptr || getSelectedSong() == nullptr)
        return juce::Result::fail ("No song selected");

    if (! canEdit())
        return juce::Result::fail ("Playback is active");

    // The playlist's songs all share their mix names, so every one of them changes.
    const auto trimmed = name.trim();
    const auto result = playlist->renameMix (mix, trimmed);

    if (result.wasOk())
        for (const auto& song : playlist->getSongs())
            markSongDirty (m_playlist, song.getName());

    report (result, "Mix " + juce::String (mix + 1) + " renamed to " + trimmed + " in every song of " + m_playlist);
    changed();
    return result;
}

juce::Result Session::setMixOutputs (int mix, MBoxTrack::Outputs outputs, const juce::String& outputsText)
{
    std::vector<int> displaced, displacedDiSends;
    juce::StringArray names, diNames;

    const auto result = editSong ([&] (MBoxSong& song)
                                  {
                                      auto r = song.setMixOutputs (mix, outputs, &displaced, &displacedDiSends);

                                      for (auto other : displaced)
                                          names.add (song.getMix (other).getName());

                                      for (auto trackId : displacedDiSends)
                                          diNames.add (juce::String (song.getTrack (trackId)->getName()));

                                      return r;
                                  });

    if (result.wasOk())
    {
        m_latencyStale = true;   // a mix may have become N/A, or come back
        auto* song = getSelectedSong();
        auto message = song->getMix (mix).getName() + " goes to " + outputsText;

        if (! names.isEmpty())
            message << "; " << names.joinIntoString (", ") << (names.size() > 1 ? " were" : " was") << " using it and now go"
                    << (names.size() > 1 ? "" : "es") << " nowhere (N/A)";

        if (! diNames.isEmpty())
            message << "; DI send off for " << diNames.joinIntoString (", ");

        setStatus (message);
    }

    return result;
}

juce::Result Session::setTrackDiSend (int trackId, MBoxTrack::Outputs outputs, const juce::String& outputsText)
{
    return editSong ([&] (MBoxSong& song) { return song.setTrackDiSend (trackId, outputs); },
                     outputs == MBoxTrack::noDiSend ? juce::String ("DI send off") : "DI send to " + outputsText);
}

Bar Session::songStartCursor()
{
    auto* song = getSelectedSong();
    return song != nullptr ? Bar { song->getStartBar(), 1, 1, 0 } : Bar {};
}

void Session::setCursor (const Bar& position)
{
    m_cursor = position;

    // Monitored inputs take the bankslot in force at the cursor while stopped.
    if (auto* engine = m_app.getAudioEngine())
        engine->setMonitorPosition (position);

    changed();
}

void Session::validateSelection()
{
    if (getSelectedPlaylist() == nullptr)
    {
        m_playlist = {};
        m_song = {};
    }

    if (getSelectedSong() == nullptr)
        m_song = {};

    if (auto* song = getSelectedSong())
    {
        if (song->getTrack (m_trackId) == nullptr)
            m_trackId = song->getTracks().front().getId();

        if (m_cursor.bar > song->getTotalBars())
            m_cursor = songStartCursor();
    }
    else
    {
        m_trackId = 0;
    }
}

//==============================================================================
void Session::setStatus (const juce::String& message, bool isError)
{
    m_status = message;
    m_statusIsError = isError;
    changed();
}

bool Session::report (const juce::Result& result, const juce::String& successMessage)
{
    if (result.failed())
        setStatus (result.getErrorMessage(), true);
    else if (successMessage.isNotEmpty())
        setStatus (successMessage);

    return result.wasOk();
}

//==============================================================================
bool Session::canEdit()
{
    if (! m_app.isPlaybackActive())
        return true;

    setStatus ("Stop playback to edit (track volume and the metronome change live).", true);
    return false;
}

bool Session::isEditLocked() const
{
    return m_previewLocked || m_app.isPlaybackActive();
}

juce::Result Session::editSong (const std::function<juce::Result (MBoxSong&)>& edit, const juce::String& successMessage)
{
    auto* song = getSelectedSong();

    if (song == nullptr)
        return juce::Result::fail ("No song selected");

    if (! canEdit())
        return juce::Result::fail ("Playback is active");

    const auto bankslotsBefore = physicalBankslotsKey (*song);
    const auto result = edit (*song);

    if (result.wasOk())
        markSongDirty (m_playlist, m_song);

    // A physical track's bankslot added or removed (or a track becoming / no longer physical).
    if (physicalBankslotsKey (*song) != bankslotsBefore)
        m_latencyStale = true;

    report (result, successMessage);
    changed();
    return result;
}

juce::Result Session::setTrackMute (int trackId, bool mute)
{
    const auto result = m_app.setTrackMute (m_playlist, m_song, m_mix, trackId, mute);
    const auto wasDirty = isSongDirty (m_playlist, m_song);

    if (result.wasOk())
        markSongDirty (m_playlist, m_song);
    else
        reportQuietly (result);

    liveChanged (! wasDirty && isSongDirty (m_playlist, m_song));
    return result;
}

juce::Result Session::setTrackSolo (int trackId, bool solo)
{
    const auto result = m_app.setTrackSolo (m_playlist, m_song, m_mix, trackId, solo);
    const auto wasDirty = isSongDirty (m_playlist, m_song);

    if (result.wasOk())
        markSongDirty (m_playlist, m_song);
    else
        reportQuietly (result);

    liveChanged (! wasDirty && isSongDirty (m_playlist, m_song));
    return result;
}

juce::Result Session::setTrackBalance (int trackId, int balance)
{
    const auto result = m_app.setTrackBalance (m_playlist, m_song, m_mix, trackId, balance);
    const auto wasDirty = isSongDirty (m_playlist, m_song);

    if (result.wasOk())
        markSongDirty (m_playlist, m_song);
    else
        reportQuietly (result);

    liveChanged (! wasDirty && isSongDirty (m_playlist, m_song));
    return result;
}

juce::Result Session::editMetronome (const std::function<juce::Result (MBoxSong&)>& edit, const juce::String& successMessage,
                                     SongChange songChange)
{
    auto* song = getSelectedSong();

    if (song == nullptr)
        return juce::Result::fail ("No song selected");

    const auto result = edit (*song);
    const auto wasDirty = isSongDirty (m_playlist, m_song);

    if (result.wasOk())
    {
        if (songChange == SongChange::yes)
            markSongDirty (m_playlist, m_song);

        m_app.applyLiveMetronome (m_playlist, m_song);
    }

    reportQuietly (result, successMessage);

    // The countdown is read when the song loads: that one reloads it.
    if (songChange == SongChange::yes)
        changed();
    else
        liveChanged (! wasDirty && isSongDirty (m_playlist, m_song));

    return result;
}

juce::Result Session::editTrack (const std::function<juce::Result (MBoxTrack&)>& edit, const juce::String& successMessage)
{
    const auto trackId = m_trackId;

    return editSong ([&] (MBoxSong& song)
                     {
                         auto* track = song.getTrack (trackId);
                         return track != nullptr ? edit (*track) : juce::Result::fail ("No track selected");
                     },
                     successMessage);
}

juce::Result Session::editBanks (const std::function<juce::Result (MBoxApplication::Banks&)>& edit, const juce::String& successMessage)
{
    if (! canEdit())
        return juce::Result::fail ("Playback is active");

    const auto result = edit (m_app.getBanks());

    if (result.wasOk())
        m_banksDirty = true;

    report (result, successMessage);
    changed();
    return result;
}

void Session::markBanksChanged()
{
    if (std::exchange (m_banksDirty, true))
        return;

    changed();
}

void Session::setSlotLoudness (BankSlotRef ref, std::optional<float> lufs)
{
    auto& slot = m_app.getBanks()[(size_t) ref.bank].m_bankSlots[(size_t) ref.slot];

    if (slot.m_loudness == lufs)
        return;

    slot.m_loudness = lufs;
    markBanksChanged();
}

MBoxInserts* Session::getInserts (int stripId)
{
    auto* song = getSelectedSong();

    if (song == nullptr)
        return nullptr;

    auto& mix = song->getMix (m_mix);

    if (stripId == masterStrip)
        return &mix.getMaster().getInserts();

    return song->getTrack (stripId) != nullptr ? &mix.getChannel (stripId).getInserts() : nullptr;
}

juce::Result Session::editInserts (int stripId, const std::function<juce::Result (MBoxInserts&)>& edit, const juce::String& successMessage)
{
    // A strip plugin added or removed: the latency is calculated again once the engine has reloaded.
    m_latencyStale = true;

    return editSong ([&] (MBoxSong&)
                     {
                         auto* inserts = getInserts (stripId);
                         return inserts != nullptr ? edit (*inserts) : juce::Result::fail ("That mixer strip no longer exists");
                     },
                     successMessage);
}

std::optional<MixLatency> Session::getSelectedMixLatency() const
{
    if (! m_latencies.has_value())
        return std::nullopt;

    return (*m_latencies)[(size_t) m_mix];
}

void Session::refreshLatencyIfStale()
{
    if (! m_latencyStale || m_song.isEmpty())
        return;

    if (auto latencies = m_app.computeLatencies (m_playlist, m_song))
    {
        m_latencies = latencies;
        m_latencyStale = false;
        m_listeners.call ([] (Listener& l) { l.sessionChanged(); });
    }
}

juce::String Session::physicalBankslotsKey (const MBoxSong& song)
{
    juce::String key;

    for (const auto& track : song.getTracks())
    {
        if (track.getTrackType() != TrackType::Physical)
            continue;

        key << track.getId() << ':';

        for (const auto& [bar, ref] : track.getBankSlots())
            key << bar.toString() << '=' << ref.bank << '.' << ref.slot << ' ';

        key << '|';
    }

    return key;
}

void Session::markSongChanged()
{
    if (getSelectedSong() == nullptr || isSongDirty (m_playlist, m_song))
        return;

    markSongDirty (m_playlist, m_song);
    changed();
}

void Session::closeInsertWindows()
{
    auto* song = getSelectedSong();

    if (song == nullptr)
        return;

    auto close = [] (MBoxInserts& inserts)
    {
        for (int i = 0; i < inserts.size(); ++i)
            if (auto* plugin = dynamic_cast<MAudioEffect*> (&inserts[i]))
                plugin->closeEditor();
    };

    for (int m = 0; m < MBoxSong::numMixes; ++m)
    {
        auto& mix = song->getMix (m);

        for (const auto& track : song->getTracks())
            close (mix.getChannel (track.getId()).getInserts());

        close (mix.getMaster().getInserts());
    }
}

juce::Result Session::setClickInMix (int mix, bool on)
{
    auto* song = getSelectedSong();
    const auto name = song != nullptr && mix >= 0 && mix < MBoxSong::numMixes ? song->getMix (mix).getName() : juce::String();

    return editMetronome ([mix, on] (MBoxSong& s) { return s.getMetronome().setPlaysIn (mix, on); },
                          (on ? "The click plays in " : "No click in ") + name, SongChange::yes);
}

juce::Result Session::setTrackMonitored (int trackId, bool monitor)
{
    const auto result = m_app.setTrackMonitored (m_playlist, m_song, trackId, monitor);
    const auto wasDirty = isSongDirty (m_playlist, m_song);

    if (result.wasOk())
        markSongDirty (m_playlist, m_song);

    reportQuietly (result, juce::String ("Track ") + juce::String (trackId) + (monitor ? ": input heard while stopped" : ": input silent while stopped"));
    liveChanged (! wasDirty && isSongDirty (m_playlist, m_song));
    return result;
}

juce::Result Session::setTrackVolume (int trackId, float decibels)
{
    const auto result = m_app.setTrackVolume (m_playlist, m_song, m_mix, trackId, decibels);
    const auto wasDirty = isSongDirty (m_playlist, m_song);

    if (result.wasOk())
        markSongDirty (m_playlist, m_song);
    else
        reportQuietly (result);

    liveChanged (! wasDirty && isSongDirty (m_playlist, m_song));
    return result;
}

juce::Result Session::setMasterVolume (float decibels)
{
    const auto result = m_app.setMasterVolume (m_playlist, m_song, m_mix, decibels);
    const auto wasDirty = isSongDirty (m_playlist, m_song);

    if (result.wasOk())
        markSongDirty (m_playlist, m_song);
    else
        reportQuietly (result);

    liveChanged (! wasDirty && isSongDirty (m_playlist, m_song));
    return result;
}

juce::Result Session::setMasterBalance (int balance)
{
    const auto result = m_app.setMasterBalance (m_playlist, m_song, m_mix, balance);
    const auto wasDirty = isSongDirty (m_playlist, m_song);

    if (result.wasOk())
        markSongDirty (m_playlist, m_song);
    else
        reportQuietly (result);

    liveChanged (! wasDirty && isSongDirty (m_playlist, m_song));
    return result;
}

//==============================================================================
juce::Result Session::createPlaylist (const juce::String& name)
{
    const auto result = m_app.createPlaylist (name);

    if (report (result, "Playlist " + name + " created"))
        selectPlaylist (name);

    changed();
    return result;
}

juce::Result Session::renamePlaylist (const juce::String& oldName, const juce::String& newName)
{
    if (! canEdit())
        return juce::Result::fail ("Playback is active");

    const auto result = m_app.updatePlaylist (oldName, newName);

    // Follow the rename before reporting: reporting notifies the panels, which validate the selection.
    if (result.wasOk())
    {
        std::set<juce::String> renamed;

        for (const auto& key : m_dirtySongs)
            renamed.insert (key.startsWith (oldName + "\n") ? newName + key.fromFirstOccurrenceOf ("\n", true, false) : key);

        m_dirtySongs = std::move (renamed);

        if (m_playlist == oldName)
            m_playlist = newName;

        if (m_copiedPlaylist == oldName)
            m_copiedPlaylist = newName;

        if (auto remembered = m_mixByPlaylist.extract (oldName); ! remembered.empty())
        {
            remembered.key() = newName;
            m_mixByPlaylist.insert (std::move (remembered));
        }
    }

    report (result, "Playlist renamed to " + newName);
    changed();
    return result;
}

juce::Result Session::deletePlaylist (const juce::String& name)
{
    if (! canEdit())
        return juce::Result::fail ("Playback is active");

    const auto result = m_app.removePlaylist (name);

    if (report (result, "Playlist " + name + " deleted"))
    {
        clearDirtyPlaylist (name);
        m_mixByPlaylist.erase (name);

        if (m_copiedPlaylist == name)
            m_copiedPlaylist.clear();
    }

    changed();
    return result;
}

void Session::copyPlaylist (const juce::String& name)
{
    if (m_app.getPlaylist (name) == nullptr)
        return;

    m_copiedPlaylist = name;
    setStatus ("Playlist " + name + " copied: paste it to make an independent copy (songs, mixes and WAVs)");
}

bool Session::canPastePlaylist() const
{
    return m_copiedPlaylist.isNotEmpty() && m_app.getPlaylist (m_copiedPlaylist) != nullptr && ! isEditLocked();
}

juce::Result Session::pastePlaylist()
{
    if (m_copiedPlaylist.isEmpty() || m_app.getPlaylist (m_copiedPlaylist) == nullptr)
        return juce::Result::fail ("Copy a playlist first");

    if (! canEdit())
        return juce::Result::fail ("Playback is active");

    const auto name = m_app.makeCopyName (m_copiedPlaylist);
    const auto result = m_app.copyPlaylist (m_copiedPlaylist, name);

    if (report (result, "Playlist " + m_copiedPlaylist + " pasted as " + name))
        selectPlaylist (name);

    changed();
    return result;
}

juce::Result Session::createSong (const juce::String& playlist, const juce::String& name)
{
    const auto result = m_app.createSong (playlist, name);

    if (report (result, "Song " + name + " created"))
        selectSong (playlist, name);

    changed();
    return result;
}

juce::Result Session::renameSong (const juce::String& playlist, const juce::String& oldName, const juce::String& newName)
{
    if (! canEdit())
        return juce::Result::fail ("Playback is active");

    const auto result = m_app.updateSong (playlist, oldName, newName);

    if (result.wasOk())
    {
        if (m_dirtySongs.erase (songKey (playlist, oldName)) > 0)
            markSongDirty (playlist, newName);

        if (m_playlist == playlist && m_song == oldName)
            m_song = newName;
    }

    report (result, "Song renamed to " + newName);
    changed();
    return result;
}

juce::Result Session::deleteSong (const juce::String& playlist, const juce::String& name)
{
    if (! canEdit())
        return juce::Result::fail ("Playback is active");

    const auto result = m_app.removeSong (playlist, name);

    if (report (result, "Song " + name + " deleted"))
        m_dirtySongs.erase (songKey (playlist, name));

    changed();
    return result;
}

juce::Result Session::importWav (int trackId, const Bar& start, const juce::File& file)
{
    if (! canEdit())
        return juce::Result::fail ("Playback is active");

    auto* song = getSelectedSong();
    const auto barsBefore = song != nullptr ? song->getTotalBars() : 0;
    const auto result = m_app.importWav (m_playlist, m_song, trackId, start, file);

    if (result.wasOk())
    {
        markSongDirty (m_playlist, m_song);
        const auto barsAfter = song->getTotalBars();
        setStatus (file.getFileName() + " placed at bar " + start.toString()
                   + (barsAfter != barsBefore ? ", song extended to " + juce::String (barsAfter) + " bars" : juce::String()));
    }
    else
    {
        report (result);
    }

    changed();
    return result;
}

juce::Result Session::moveWav (int trackId, const Bar& start)
{
    if (! canEdit())
        return juce::Result::fail ("Playback is active");

    auto* song = getSelectedSong();
    const auto barsBefore = song != nullptr ? song->getTotalBars() : 0;
    const auto result = m_app.moveWav (m_playlist, m_song, trackId, start);

    if (result.wasOk())
    {
        markSongDirty (m_playlist, m_song);
        const auto barsAfter = song->getTotalBars();
        setStatus ("WAV moved to " + start.toString()
                   + (barsAfter != barsBefore ? ", song extended to " + juce::String (barsAfter) + " bars" : juce::String()));
    }
    else
    {
        report (result);
    }

    changed();
    return result;
}

juce::Result Session::loadFolder (const juce::File& folder)
{
    stop();
    closeInsertWindows();
    const auto result = m_app.loadFrom (folder);

    if (result.wasOk())
    {
        m_dirtySongs.clear();
        m_banksDirty = false;
        selectFirstSong();

        const auto& warnings = m_app.getWarnings();
        setStatus (warnings.isEmpty() ? "Loaded " + folder.getFullPathName()
                                      : "Loaded with " + juce::String (warnings.size()) + " warning(s): " + warnings[0],
                   ! warnings.isEmpty());
    }
    else
    {
        report (result);
    }

    changed();
    return result;
}

//==============================================================================
bool Session::isSongDirty (const juce::String& playlist, const juce::String& song) const
{
    return m_dirtySongs.count (songKey (playlist, song)) > 0;
}

bool Session::isPlaylistDirty (const juce::String& playlist) const
{
    const auto prefix = playlist + "\n";
    return std::any_of (m_dirtySongs.begin(), m_dirtySongs.end(), [&] (const auto& key) { return key.startsWith (prefix); });
}

void Session::markSongDirty (const juce::String& playlist, const juce::String& song)
{
    if (playlist.isNotEmpty() && song.isNotEmpty())
        m_dirtySongs.insert (songKey (playlist, song));
}

void Session::clearDirtyPlaylist (const juce::String& playlist)
{
    const auto prefix = playlist + "\n";

    for (auto it = m_dirtySongs.begin(); it != m_dirtySongs.end();)
        it = it->startsWith (prefix) ? m_dirtySongs.erase (it) : std::next (it);
}

juce::Result Session::saveSelectedSong()
{
    const auto result = m_app.saveSong (m_playlist, m_song);

    if (report (result, "Saved " + m_song))
    {
        m_dirtySongs.erase (songKey (m_playlist, m_song));
        m_banksDirty = false;
    }

    changed();
    return result;
}

juce::Result Session::savePlaylist (const juce::String& playlist)
{
    const auto result = m_app.save (playlist);

    if (report (result, "Saved playlist " + playlist))
    {
        clearDirtyPlaylist (playlist);
        m_banksDirty = false;
    }

    changed();
    return result;
}

juce::Result Session::saveAll()
{
    for (const auto& playlist : m_app.getPlaylists())
    {
        if (! isPlaylistDirty (playlist.getName()))
            continue;

        if (auto r = m_app.save (playlist.getName()); r.failed())
        {
            report (r);
            changed();
            return r;
        }

        clearDirtyPlaylist (playlist.getName());
        m_banksDirty = false;
    }

    if (m_banksDirty)
    {
        if (auto r = m_app.saveBanks(); r.failed())
        {
            report (r);
            changed();
            return r;
        }

        m_banksDirty = false;
    }

    setStatus ("All changes saved");
    return juce::Result::ok();
}

//==============================================================================
juce::Result Session::play()
{
    cancelAutoNext();

    if (getSelectedSong() == nullptr)
        return juce::Result::fail ("No song selected");

    const auto result = m_app.play (m_playlist, m_song, m_cursor);
    refreshLatencyIfStale();   // play() loads the song if the background load hadn't yet
    report (result);
    changed();
    return result;
}

juce::Result Session::playFromCursor()
{
    const auto* engine = std::as_const (m_app).getAudioEngine();

    if (engine == nullptr || ! engine->isActive())
        return play();

    if (getSelectedSong() == nullptr)
        return juce::Result::fail ("No song selected");

    // Playing again at once would move the players while the stop still fades out from where they were.
    stop();
    changed();

    const auto token = m_autoNextToken;   // a play, stop or selection meanwhile cancels the restart
    const auto delayMs = (int) std::ceil (MBoxAudioEngine::fadeSeconds * 1000.0) + 20;

    juce::Timer::callAfterDelay (delayMs, [this, alive = std::weak_ptr<bool> (m_alive), token]
    {
        if (! alive.expired() && token == m_autoNextToken)
            play();
    });

    return juce::Result::ok();
}

void Session::pause()
{
    m_app.pause();
    changed();
}

void Session::stop()
{
    if (m_waitingForNext)
        setStatus ("Next song cancelled");

    cancelAutoNext();   // also cancels a pending restart from playFromCursor()

    if (m_app.getAudioEngine() == nullptr)
        return;

    m_app.stopPlayback();
}

void Session::songReachedEnd()
{
    m_app.stopPlayback();

    auto* playlist = getSelectedPlaylist();
    auto* song = getSelectedSong();

    if (playlist == nullptr || song == nullptr || song->getEndAction() == MBoxSong::EndAction::stop)
    {
        setStatus ("End of song");
        return;
    }

    const auto& songs = playlist->getSongs();
    const auto current = std::find_if (songs.begin(), songs.end(), [&] (const MBoxSong& s) { return s.getName() == m_song; });

    if (current == songs.end() || std::next (current) == songs.end())
    {
        setStatus ("End of the playlist");
        return;
    }

    const auto delay = song->getNextDelay();
    const auto nextName = std::next (current)->getName();
    const auto playlistName = m_playlist;

    // Selecting it now shows it and loads it during the delay, so it starts on time.
    selectSong (playlistName, nextName);
    const auto token = ++m_autoNextToken;
    m_waitingForNext = true;
    setStatus ("Up next: " + nextName + ", starting in " + juce::String (delay) + (delay == 1 ? " second" : " seconds"));

    juce::Timer::callAfterDelay (delay * 1000, [this, alive = std::weak_ptr<bool> (m_alive), token, playlistName, nextName]
    {
        if (alive.expired() || token != m_autoNextToken || m_playlist != playlistName || m_song != nextName)
            return;

        m_waitingForNext = false;
        m_cursor = songStartCursor();

        if (play().wasOk())
            setStatus ("Playing " + nextName);
    });
}

juce::Result Session::setSongEnd (const juce::String& playlistName, const juce::String& songName, MBoxSong::EndAction action, int delay)
{
    auto* playlist = m_app.getPlaylist (playlistName);
    auto* song = playlist != nullptr ? playlist->getSong (songName) : nullptr;

    if (song == nullptr)
        return juce::Result::fail ("Song " + songName + " does not exist");

    if (auto r = song->setNextDelay (delay); r.failed())
    {
        report (r);
        return r;
    }

    song->setEndAction (action);
    markSongDirty (playlistName, songName);
    setStatus (songName + (action == MBoxSong::EndAction::stop ? ": stops at its end"
                                                               : ": plays the next song " + juce::String (delay) + " s after its end"));
    return juce::Result::ok();
}

bool Session::isPlaying() const
{
    const auto* engine = std::as_const (m_app).getAudioEngine();
    return engine != nullptr && engine->getState() == MBoxAudioEngine::State::Playing;
}
