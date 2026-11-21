#pragma once

#include "MBoxSong.h"

//==============================================================================
// Checks that a playlist or song name can be used as a folder name on Windows, macOS and
// Linux: not empty, at most 64 characters, none of < > : " / \ | ? * or control characters,
// no leading or trailing space, no trailing dot, and not a reserved Windows device name.
//==============================================================================
juce::Result validateItemName (const juce::String& name);

//==============================================================================
// An ordered list of songs. Song names are unique within a playlist, ignoring case (macOS and
// Windows file systems ignore case, and each song is stored in a folder named after it).
//==============================================================================
class MBoxPlaylist
{
public:
    explicit MBoxPlaylist (const juce::String& name);

    // Deep copy, used for the loader snapshot
    MBoxPlaylist (const MBoxPlaylist& other);
    MBoxPlaylist& operator= (const MBoxPlaylist&) = delete;

    juce::String getName() const;

    // Renames the playlist itself. Uniqueness among playlists is checked by MBoxCoreAudio.
    juce::Result rename (const juce::String& newName);

    // Adds a new, empty song at the end
    juce::Result addSong (const juce::String& songName);
    // Adds an existing song (used when loading from storage)
    juce::Result addSong (std::unique_ptr<MBoxSong> song);
    juce::Result removeSong (const juce::String& songName);
    juce::Result renameSong (const juce::String& songName, const juce::String& newName);
    // Moves a song to a 0-based position in the list
    juce::Result moveSong (const juce::String& songName, int to);

    MBoxSong* getSongInstance (const juce::String& songName) const;
    int getSongIndex (const juce::String& songName) const;
    juce::StringArray getSongNames() const;
    const std::vector<std::unique_ptr<MBoxSong>>& getSongs() const noexcept  { return m_songs; }

private:
    juce::String m_name;
    std::vector<std::unique_ptr<MBoxSong>> m_songs;
};
