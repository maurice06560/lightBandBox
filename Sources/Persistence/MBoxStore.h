#pragma once

#include "../Model/MBoxPlaylist.h"

#include <juce_core/juce_core.h>

#include <memory>
#include <vector>

/** Reads the playlists, songs and tracks stored under the audio folder:

    <audioFolder>/playlists/playlists.json                                    list of playlists and their songs
    <audioFolder>/playlists/<playlist>/songs/<song>/song.json                 locators, tempo and signature grids, mixes
    <audioFolder>/playlists/<playlist>/songs/<song>/tracks/track.json         "Wav" (audio file) and "Physical" (input) tracks
    <audioFolder>/playlists/<playlist>/songs/<song>/tracks/media/<file>.wav   audio files
*/
class MBoxStore final
{
public:
    explicit MBoxStore (const juce::File& audioFolder) : m_audioFolder (audioFolder) {}
    ~MBoxStore() = default;

    /** Parses playlists.json and every song it lists. Returns the playlists in file order.
        A song whose files can't be read is still added (empty), and the error is logged. */
    std::vector<std::unique_ptr<MBoxPlaylist>> loadPlaylists() const;

private:
    juce::File getPlaylistsFolder() const { return m_audioFolder.getChildFile ("playlists"); }

    /** Fills song from <songFolder>/song.json and <songFolder>/tracks/track.json. */
    void loadSong (const juce::File& songFolder, MBoxSong& song) const;

    /** Adds the "Wav" and "Physical" tracks of track.json that are part of the given mix. */
    void loadTracks (const juce::File& tracksFolder, const juce::var& mix, MBoxSong& song) const;

    /** Adds a "Wav" track: its audio file (relative to tracksFolder) and start position. */
    void loadAudioTrack (const juce::File& tracksFolder, const juce::var& track, MBoxSong& song) const;

    /** Adds a "Physical" track: its physical input. */
    void loadPhysicalTrack (const juce::var& track, MBoxSong& song) const;

    const juce::File m_audioFolder;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MBoxStore)
};
