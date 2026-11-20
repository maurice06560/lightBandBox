#pragma once

#include "../Model/MBoxPlaylist.h"
#include "../Model/MBoxRig.h"

using MBoxPlaylists = std::vector<std::unique_ptr<MBoxPlaylist>>;

//==============================================================================
// Loads and saves the data folder:
//
//   <root>/banks.json                                   guitar effect banks (global)
//   <root>/rig.json                                     mix bus outputs and masters, audio device
//   <root>/playlists/playlists.json                     playlist order and song order
//   <root>/playlists/<Playlist>/songs/<Song>/song.json  tempo, signatures, locators, metronome, mixes
//   <root>/playlists/<Playlist>/songs/<Song>/tracks/track.json   the track list
//   <root>/playlists/<Playlist>/songs/<Song>/tracks/media/*.wav  backing audio files
//
// Every file has a "schemaVersion". Every write goes to a temporary file that then replaces
// the original, so a crash or power cut mid-save leaves the previous version intact.
// Removed playlists and songs are moved to <root>/.trash rather than deleted, since their
// folders hold the user's WAV files.
// Positions are written as "bar.beat.division.tick" strings.
//==============================================================================
class MBoxStorage
{
public:
    static constexpr int schemaVersion = 1;

    explicit MBoxStorage (const juce::File& root);

    // ONE_BOX_DATA_DIR when set, otherwise <user home>/OneBox
    static juce::File getDefaultDataDirectory();

    juce::File getRoot() const  { return m_root; }
    juce::File getPlaylistDirectory (const juce::String& playlist) const;
    juce::File getSongDirectory (const juce::String& playlist, const juce::String& song) const;
    juce::File getTracksDirectory (const juce::String& playlist, const juce::String& song) const;
    juce::File getMediaDirectory (const juce::String& playlist, const juce::String& song) const;

    // Loads everything. Missing files give defaults (a first start has an empty folder). Fails
    // on the first malformed file, naming it, and leaves the outputs in an unspecified state.
    juce::Result load (MBoxPlaylists& playlists, MBoxRig& rig, MBoxBanks& banks) const;

    // Writes every file
    juce::Result save (const MBoxPlaylists& playlists, const MBoxRig& rig, const MBoxBanks& banks) const;

    // Folder operations done immediately by the matching API calls, so a song's media files
    // follow it. Renaming a folder that doesn't exist yet (never saved) succeeds.
    juce::Result renamePlaylistDirectory (const juce::String& oldName, const juce::String& newName) const;
    juce::Result renameSongDirectory (const juce::String& playlist, const juce::String& oldName, const juce::String& newName) const;
    juce::Result trashPlaylistDirectory (const juce::String& playlist) const;
    juce::Result trashSongDirectory (const juce::String& playlist, const juce::String& song) const;

    //==========================================================================
    // JSON conversions, public for the tests. tracksDirectory is where relative WAV paths start.
    static juce::var songToJson (const MBoxSong& song);
    static juce::Result songFromJson (const juce::var& json, MBoxSong& song);
    static juce::var tracksToJson (const MBoxSong& song, const juce::File& tracksDirectory);
    static juce::Result tracksFromJson (const juce::var& json, MBoxSong& song, const juce::File& tracksDirectory);
    static juce::var rigToJson (const MBoxRig& rig);
    static juce::Result rigFromJson (const juce::var& json, MBoxRig& rig);
    static juce::var banksToJson (const MBoxBanks& banks);
    static juce::Result banksFromJson (const juce::var& json, MBoxBanks& banks);
    static juce::var effectsToJson (const std::vector<MBoxEffect>& effects);
    static juce::Result effectsFromJson (const juce::var& json, std::vector<MBoxEffect>& effects);

    // Writes a JSON file through a temporary file
    static juce::Result writeJsonFile (const juce::File& file, const juce::var& json);
    // Reads a JSON file and checks its schema version. A missing file returns ok and a void var.
    static juce::Result readJsonFile (const juce::File& file, juce::var& json);

private:
    juce::Result moveToTrash (const juce::File& directory, const juce::String& label) const;

    juce::File m_root;
};
