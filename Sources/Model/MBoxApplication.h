#pragma once

#include "../Audio/MBoxAudioEngine.h"
#include "MBoxPlaylist.h"

#include <juce_core/juce_core.h>

#include <memory>
#include <vector>

class MBoxApplication final
{
public:
    MBoxApplication() = default;
    ~MBoxApplication() = default;

    void selectSong(const juce::String& playlistName, const juce::String& song);

    // Transport
    void play();
    void pause();
    void stop();

    // Getters
    juce::StringArray getPlaylistNames() const;
    /** Returns the song names of the given playlist, or an empty array if the playlist doesn't exist. */
    juce::StringArray getSongNames (const juce::String& playlistName) const;

    MBoxPlaylist* getPlaylist(const juce::String& playlistName) const;
    MBoxSong* getSong(const juce::String& playlistName, const juce::String& songName) const;

    // Setters
    bool addPlaylist (const juce::String& name);
    bool addSong (const juce::String& playlistName, const juce::String& songName);

    // Test
    /** Fills the model with a test playlist, song and audio track. */
    void testApplication();

private:
    MBoxAudioEngine m_audioEngine;
    std::vector<std::unique_ptr<MBoxPlaylist>> m_playlists;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MBoxApplication)
};
