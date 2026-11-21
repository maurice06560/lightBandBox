#include "Session.h"

#include <Audio/MBoxAudioEngine.h>
#include <Persistence/MBoxStorage.h>
#include <TCPServer/MBoxComAPI.h>

namespace
{
    juce::Result toResult (const MBoxResult& result)
    {
        return result.wasOk() ? juce::Result::ok() : juce::Result::fail (juce::String (result.m_message));
    }

    juce::StringArray toStringArray (const std::vector<std::string>& names)
    {
        juce::StringArray array;

        for (const auto& name : names)
            array.add (juce::String (name));

        return array;
    }

    std::string str (const juce::String& text)  { return text.toStdString(); }
}

//==============================================================================
Session::Session (MBoxCoreAudio& core)
    : m_core (core)
{
    m_transport = m_core.getTransportInfo();
    m_eventListenerId = m_core.addEventListener ([this] (const MBoxEvent& event) { handleEvent (event); });
}

Session::~Session()
{
    m_core.removeEventListener (m_eventListenerId);
}

//==============================================================================
void Session::changed()
{
    validateSelection();
    m_listeners.call ([] (Listener& l) { l.sessionChanged(); });
}

void Session::showInList (ListTab tab, const Bar& position)
{
    m_listeners.call ([&] (Listener& l) { l.showInList (tab, position); });
}

//==============================================================================
// Library events (message thread)
//==============================================================================
void Session::handleEvent (const MBoxEvent& event)
{
    const auto data = juce::JSON::parse (juce::String (event.m_json));
    const auto& name = event.m_name;

    if (name == MBoxComAPI::Event::transport)
    {
        const auto wasLocked = isEditLocked();
        m_transport = m_core.getTransportInfo();

        // Starting or stopping locks or unlocks the editors
        if (isEditLocked() != wasLocked)
            changed();

        m_listeners.call ([] (Listener& l) { l.sessionTransportChanged(); });
    }
    else if (name == MBoxComAPI::Event::playlistLoading)
    {
        m_transport = m_core.getTransportInfo();
        const auto percent = juce::roundToInt ((double) data["progress"] * 100.0);
        setStatus ("Loading " + data["playlist"].toString() + "... " + juce::String (percent) + "%");
    }
    else if (name == MBoxComAPI::Event::playlistLoaded)
    {
        m_transport = m_core.getTransportInfo();
        m_listeners.call ([] (Listener& l) { l.sessionPlayerReplaced(); });

        if (data["playlist"].toString().isEmpty())
            changed();
        else if ((bool) data["ok"])
            setStatus (data["playlist"].toString() + " is ready");
        else
            setStatus ("Loading " + data["playlist"].toString() + " failed: " + data["error"].toString(), true);
    }
    else if (name == MBoxComAPI::Event::songChanged)
    {
        const auto playlist = data["playlist"].toString();
        const auto song = data["song"].toString();
        m_transport = m_core.getTransportInfo();

        if (m_pendingPlaylist.isNotEmpty())
        {
            // Waiting for our own selection: other songs are the previous playlist moving on
            if (! playlist.equalsIgnoreCase (m_pendingPlaylist) || ! song.equalsIgnoreCase (m_pendingSong))
                return;

            m_pendingPlaylist.clear();
            m_pendingSong.clear();
        }

        // The playlist moved on to its next song (Continue), or a TCP client selected one
        if (song.isNotEmpty() && (playlist != m_playlist || song != m_song))
        {
            m_playlist = playlist;
            m_song = song;
            m_trackId = 0;
            m_cursor = songStartCursor();
            changed();
        }
    }
    else if (name == MBoxComAPI::Event::mixChanged)
    {
        m_listeners.call ([] (Listener& l) { l.sessionMixChanged(); });
    }
    else if (name == MBoxComAPI::Event::deviceChanged)
    {
        changed();
    }
    else if (name == MBoxComAPI::Event::error)
    {
        setStatus (data["message"].toString(), true);
    }
}

//==============================================================================
// Playlists, songs and selection
//==============================================================================
juce::StringArray Session::getPlaylistNames() const
{
    return toStringArray (m_core.getPlaylistNames());
}

juce::StringArray Session::getSongNames (const juce::String& playlist) const
{
    return toStringArray (m_core.getSongNames (str (playlist)));
}

MBoxSong* Session::getSelectedSong()
{
    return m_song.isNotEmpty() ? m_core.getSongInstance (str (m_playlist), str (m_song)) : nullptr;
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

    m_playlist = playlist;
    m_song = {};
    m_trackId = 0;
    m_cursor = {};
    changed();
}

void Session::selectSong (const juce::String& playlist, const juce::String& song)
{
    if (playlist == m_playlist && song == m_song)
        return;

    m_playlist = playlist;
    m_song = song;
    m_trackId = 0;
    m_cursor = songStartCursor();

    // Set first: selecting within the loaded playlist reports songChanged at once
    m_pendingPlaylist = playlist;
    m_pendingSong = song;

    if (const auto result = m_core.selectSong (str (playlist), str (song)); ! result.wasOk())
    {
        m_pendingPlaylist.clear();
        m_pendingSong.clear();
        reportQuietly (toResult (result));
    }

    m_transport = m_core.getTransportInfo();
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
    const auto info = m_core.getTransportInfo();

    if (! info.m_song.empty())
    {
        // Already loaded (a TCP client, or the previous session's selection): just show it
        m_playlist = juce::String (info.m_playlist);
        m_song = juce::String (info.m_song);
        m_trackId = 0;
        m_cursor = songStartCursor();
        changed();
        return;
    }

    const auto [playlist, song] = findFirstSong();

    if (song.isNotEmpty())
        selectSong (playlist, song);
    else
        selectPlaylist (playlist);
}

std::pair<juce::String, juce::String> Session::findFirstSong() const
{
    const auto playlists = getPlaylistNames();

    for (const auto& playlist : playlists)
        if (const auto songs = getSongNames (playlist); ! songs.isEmpty())
            return { playlist, songs[0] };

    return { playlists.isEmpty() ? juce::String() : playlists[0], {} };
}

bool Session::isSelectionLoaded() const
{
    return m_song.isNotEmpty() && ! m_transport.m_loading
        && juce::String (m_transport.m_playlist) == m_playlist && juce::String (m_transport.m_song) == m_song;
}

void Session::validateSelection()
{
    if (m_playlist.isNotEmpty() && ! getPlaylistNames().contains (m_playlist))
    {
        m_playlist = {};
        m_song = {};
    }

    if (m_song.isNotEmpty() && ! getSongNames (m_playlist).contains (m_song))
        m_song = {};

    if (auto* song = getSelectedSong())
    {
        if (song->getTrack (m_trackId) == nullptr)
            m_trackId = song->getTracks().empty() ? 0 : song->getTracks().front()->getId();

        if (m_cursor.m_bar > song->getTotalBar())
            m_cursor = songStartCursor();
    }
    else
    {
        m_trackId = 0;
    }
}

//==============================================================================
// Mix bus
//==============================================================================
void Session::selectBus (int bus)
{
    if (bus == m_bus || bus < 1 || bus > numberOfMixBus)
        return;

    m_bus = bus;
    setStatus ("Showing " + getSelectedMixBus().getName());
}

MBoxMixBus& Session::getSelectedMixBus()
{
    return getRig().m_mixBuses[(size_t) m_bus - 1];
}

MBoxTrackMix* Session::getTrackMix (int trackId)
{
    auto* song = getSelectedSong();
    return song != nullptr ? song->getTrackMix (m_bus, trackId) : nullptr;
}

//==============================================================================
// Cursor
//==============================================================================
void Session::setCursor (const Bar& position)
{
    m_cursor = position;
    changed();
}

Bar Session::songStartCursor()
{
    auto* song = getSelectedSong();
    return song != nullptr ? song->getLeftLocator() : Bar {};
}

//==============================================================================
// Status
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

juce::Result Session::fail (const juce::String& message)
{
    const auto result = juce::Result::fail (message);
    report (result);
    return result;
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

//==============================================================================
// Editing
//==============================================================================
bool Session::canEdit()
{
    if (! isEditLocked())
        return true;

    setStatus ("Stop playback to edit (the mix, the metronome and the song end change live).", true);
    return false;
}

bool Session::isEditLocked() const
{
    return m_previewLocked || m_transport.m_state != "stopped";
}

juce::Result Session::editSong (const std::function<juce::Result (MBoxSong&)>& edit, const juce::String& successMessage)
{
    auto* song = getSelectedSong();

    if (song == nullptr)
        return juce::Result::fail ("No song selected");

    if (! canEdit())
        return juce::Result::fail ("Playback is active");

    auto result = edit (*song);

    if (result.wasOk())
        result = toResult (m_core.songChanged (str (m_playlist), str (m_song)));

    reportQuietly (result, successMessage);
    changed();
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

juce::Result Session::editRig (const std::function<juce::Result (MBoxRig&)>& edit, const juce::String& successMessage)
{
    if (! canEdit())
        return juce::Result::fail ("Playback is active");

    auto result = edit (getRig());

    if (result.wasOk())
        result = toResult (m_core.rigChanged());

    reportQuietly (result, successMessage);
    changed();
    return result;
}

juce::Result Session::editBanks (const std::function<juce::Result (std::span<MBoxBank>)>& edit, const juce::String& successMessage)
{
    if (! canEdit())
        return juce::Result::fail ("Playback is active");

    auto result = edit (getBanks());

    if (result.wasOk())
        result = toResult (m_core.banksChanged());

    reportQuietly (result, successMessage);
    changed();
    return result;
}

MBoxRig& Session::getRig()
{
    return *m_core.getRig();
}

std::span<MBoxBank> Session::getBanks()
{
    return { m_core.getBanks(), (size_t) numberOfBanks };
}

std::vector<MBoxEffect>* Session::getInserts (int stripId)
{
    if (stripId == masterStrip)
        return &getSelectedMixBus().getMasterEffectChain();

    auto* trackMix = getTrackMix (stripId);
    return trackMix != nullptr ? &trackMix->m_inserts : nullptr;
}

juce::Result Session::editInserts (int stripId, const std::function<juce::Result (std::vector<MBoxEffect>&)>& edit,
                                   const juce::String& successMessage)
{
    if (stripId == masterStrip)
    {
        const auto bus = m_bus;
        return editRig ([&] (MBoxRig& rig) { return edit (rig.m_mixBuses[(size_t) bus - 1].getMasterEffectChain()); },
                        successMessage);
    }

    return editSong ([&] (MBoxSong&)
                     {
                         auto* inserts = getInserts (stripId);
                         return inserts != nullptr ? edit (*inserts) : juce::Result::fail ("That mixer strip no longer exists");
                     },
                     successMessage);
}

//==============================================================================
// Live mixing
//==============================================================================
juce::Result Session::liveMix (const MBoxResult& result)
{
    // On success the library's mixChanged event notifies the listeners
    if (! result.wasOk())
        report (toResult (result));

    return toResult (result);
}

juce::Result Session::setTrackVolume (int trackId, double volumeDb)
{
    if (! isSelectionLoaded())
        return liveMix (MBoxResult::fail ("Wait for " + str (m_song) + " to finish loading"));

    return liveMix (m_core.setTrackVolume (m_bus, trackId, volumeDb));
}

juce::Result Session::setTrackBalance (int trackId, double balance)
{
    if (! isSelectionLoaded())
        return liveMix (MBoxResult::fail ("Wait for " + str (m_song) + " to finish loading"));

    return liveMix (m_core.setTrackBalance (m_bus, trackId, balance));
}

juce::Result Session::setTrackMute (int trackId, bool mute)
{
    if (! isSelectionLoaded())
        return liveMix (MBoxResult::fail ("Wait for " + str (m_song) + " to finish loading"));

    return liveMix (m_core.setTrackMute (m_bus, trackId, mute));
}

juce::Result Session::setTrackSolo (int trackId, bool solo)
{
    if (! isSelectionLoaded())
        return liveMix (MBoxResult::fail ("Wait for " + str (m_song) + " to finish loading"));

    return liveMix (m_core.setTrackSolo (m_bus, trackId, solo));
}

juce::Result Session::setMetronomeInMix (bool on)
{
    const auto result = liveMix (m_core.setMetronomeInMix (m_bus, on));

    if (result.wasOk())
        setStatus ((on ? "The click plays in " : "No click in ") + getSelectedMixBus().getName());

    return result;
}

// The bus settings are rack-wide: they don't need a loaded song
juce::Result Session::setMasterVolume (double volumeDb)     { return liveMix (m_core.setBusVolume (m_bus, volumeDb)); }
juce::Result Session::setMasterBalance (double balance)     { return liveMix (m_core.setBusBalance (m_bus, balance)); }
juce::Result Session::setMetronomeVolume (double volumeDb)  { return liveMix (m_core.setMetronomeVolume (m_bus, volumeDb)); }

juce::Result Session::setMetronomeState (MetronomeState state)
{
    auto* song = getSelectedSong();

    if (song == nullptr)
        return juce::Result::fail ("No song selected");

    song->setMetronomeState (state);
    const auto result = toResult (m_core.songChanged (str (m_playlist), str (m_song)));

    const auto text = state == MetronomeState::Off               ? juce::String ("off")
                    : state == MetronomeState::FourCountdownOnly ? juce::String ("count-in only")
                                                                 : juce::String ("on");
    report (result, "Metronome " + text + " for " + m_song);
    return result;
}

juce::Result Session::setSongEnd (const juce::String& playlist, const juce::String& songName, EndSongAction action, int delaySeconds)
{
    auto* song = m_core.getSongInstance (str (playlist), str (songName));

    if (song == nullptr)
        return juce::Result::fail ("Song " + songName + " does not exist");

    if (delaySeconds < 0 || delaySeconds > maxEndSongDelaySeconds)
        return juce::Result::fail ("The delay goes from 0 to " + juce::String (maxEndSongDelaySeconds) + " seconds");

    song->setEndSongAction (action);
    song->setEndSongDelay (delaySeconds);
    const auto result = toResult (m_core.songChanged (str (playlist), str (songName)));

    report (result, songName + (action == EndSongAction::Stop ? ": stops at its end"
                                                              : ": plays the next song " + juce::String (delaySeconds) + " s after its end"));
    return result;
}

//==============================================================================
// Structure
//==============================================================================
juce::Result Session::createPlaylist (const juce::String& name)
{
    const auto result = toResult (m_core.addPlaylist (str (name)));

    reportQuietly (result, "Playlist " + name + " created");

    if (result.wasOk())
        selectPlaylist (name);

    changed();
    return result;
}

juce::Result Session::renamePlaylist (const juce::String& oldName, const juce::String& newName)
{
    if (! canEdit())
        return juce::Result::fail ("Playback is active");

    const auto result = toResult (m_core.renamePlaylist (str (oldName), str (newName)));

    // Follow the rename before notifying: the listeners validate the selection
    if (result.wasOk() && m_playlist.equalsIgnoreCase (oldName))
        m_playlist = newName;

    if (result.wasOk() && m_copiedPlaylist.equalsIgnoreCase (oldName))
        m_copiedPlaylist = newName;

    reportQuietly (result, "Playlist renamed to " + newName);
    changed();
    return result;
}

juce::Result Session::deletePlaylist (const juce::String& name)
{
    if (! canEdit())
        return juce::Result::fail ("Playback is active");

    const auto result = toResult (m_core.removePlaylist (str (name)));

    if (result.wasOk() && m_copiedPlaylist.equalsIgnoreCase (name))
        m_copiedPlaylist.clear();

    reportQuietly (result, "Playlist " + name + " moved to the trash");
    changed();
    return result;
}

void Session::copyPlaylist (const juce::String& name)
{
    if (! getPlaylistNames().contains (name))
        return;

    m_copiedPlaylist = name;
    setStatus ("Playlist " + name + " copied: paste it to make an independent copy (songs, mixes and WAVs)");
}

bool Session::canPastePlaylist() const
{
    return m_copiedPlaylist.isNotEmpty() && getPlaylistNames().contains (m_copiedPlaylist) && ! isEditLocked();
}

juce::Result Session::pastePlaylist()
{
    if (m_copiedPlaylist.isEmpty() || ! getPlaylistNames().contains (m_copiedPlaylist))
        return fail ("Copy a playlist first");

    if (! canEdit())
        return juce::Result::fail ("Playback is active");

    // "<name> copy", then "<name> copy 2"...
    const auto names = getPlaylistNames();
    auto name = m_copiedPlaylist + " copy";

    for (int n = 2; names.contains (name, true); ++n)
        name = m_copiedPlaylist + " copy " + juce::String (n);

    const auto result = toResult (m_core.copyPlaylist (str (m_copiedPlaylist), str (name)));
    reportQuietly (result, "Playlist " + m_copiedPlaylist + " pasted as " + name);

    if (result.wasOk())
        selectPlaylist (name);

    changed();
    return result;
}

juce::Result Session::createSong (const juce::String& playlist, const juce::String& name)
{
    const auto result = toResult (m_core.addSong (str (playlist), str (name)));

    reportQuietly (result, "Song " + name + " created");

    if (result.wasOk())
        selectSong (playlist, name);

    changed();
    return result;
}

juce::Result Session::renameSong (const juce::String& playlist, const juce::String& oldName, const juce::String& newName)
{
    if (! canEdit())
        return juce::Result::fail ("Playback is active");

    const auto result = toResult (m_core.renameSong (str (playlist), str (oldName), str (newName)));

    if (result.wasOk() && m_playlist.equalsIgnoreCase (playlist) && m_song.equalsIgnoreCase (oldName))
        m_song = newName;

    reportQuietly (result, "Song renamed to " + newName);
    changed();
    return result;
}

juce::Result Session::deleteSong (const juce::String& playlist, const juce::String& name)
{
    if (! canEdit())
        return juce::Result::fail ("Playback is active");

    const auto result = toResult (m_core.removeSong (str (playlist), str (name)));

    reportQuietly (result, "Song " + name + " moved to the trash");
    changed();
    return result;
}

juce::Result Session::importWav (int trackId, const Bar& start, const juce::File& file)
{
    auto* song = getSelectedSong();

    if (song == nullptr)
        return juce::Result::fail ("No song selected");

    if (! canEdit())
        return juce::Result::fail ("Playback is active");

    auto* track = dynamic_cast<MBoxAudioTrack*> (song->getTrack (trackId));

    if (track == nullptr)
        return fail ("Track " + juce::String (trackId) + " is not an audio track");

    const auto previousStart = track->getAudioStart();
    const auto locatorBefore = song->getRightLocator();
    auto result = song->setAudioStart (trackId, start);

    if (result.wasOk())
        result = toResult (m_core.importAudioFile (str (m_playlist), str (m_song), trackId, str (file.getFullPathName())));

    if (result.failed())
        song->setAudioStart (trackId, previousStart);

    const auto locatorAfter = song->getRightLocator();
    reportQuietly (result, file.getFileName() + " placed at " + juce::String (start.toString())
                               + (locatorAfter != locatorBefore ? ", right locator moved to " + juce::String (locatorAfter.toString())
                                                                : juce::String()));
    changed();
    return result;
}

juce::Result Session::moveWav (int trackId, const Bar& start)
{
    auto* song = getSelectedSong();
    const auto locatorBefore = song != nullptr ? song->getRightLocator() : Bar {};
    const auto result = editSong ([&] (MBoxSong& s) { return s.setAudioStart (trackId, start); });

    if (result.wasOk())
        setStatus ("WAV moved to " + juce::String (start.toString())
                   + (song->getRightLocator() != locatorBefore ? ", right locator moved to " + juce::String (song->getRightLocator().toString())
                                                               : juce::String()));
    return result;
}

juce::Result Session::saveAll()
{
    const auto result = toResult (m_core.save());
    report (result, "Saved");
    return result;
}

//==============================================================================
// Transport
//==============================================================================
juce::Result Session::play()
{
    if (getSelectedSong() == nullptr)
        return fail ("No song selected");

    if (! isSelectionLoaded())
        return fail ("Wait for " + m_song + " to finish loading");

    // Stopped: from the edit cursor. Paused: resume.
    const auto result = toResult (m_transport.m_state == "stopped" ? m_core.playFrom (m_cursor.toString()) : m_core.play());
    m_transport = m_core.getTransportInfo();
    report (result);
    changed();
    return result;
}

juce::Result Session::playFromCursor()
{
    if (getSelectedSong() == nullptr)
        return fail ("No song selected");

    if (! isSelectionLoaded())
        return fail ("Wait for " + m_song + " to finish loading");

    const auto result = toResult (m_core.playFrom (m_cursor.toString()));
    m_transport = m_core.getTransportInfo();
    report (result);
    changed();
    return result;
}

void Session::pause()
{
    m_core.pause();
    m_transport = m_core.getTransportInfo();
    changed();
}

void Session::stop()
{
    m_core.stop();
    m_transport = m_core.getTransportInfo();
    changed();
}

bool Session::isPlaying() const
{
    return m_transport.m_state == "playing";
}

std::optional<std::int64_t> Session::getPlayheadFrames() const
{
    if (! isSelectionLoaded() || m_transport.m_state == "stopped")
        return std::nullopt;

    auto* engine = m_core.getAudioEngine();

    if (engine == nullptr)
        return std::nullopt;

    const auto status = engine->getRenderer().getStatus();

    if (status.m_state == TransportState::Stopped)
        return std::nullopt;

    return status.m_position;
}

juce::File Session::getDataDirectory() const
{
    return MBoxStorage::getDefaultDataDirectory();
}
