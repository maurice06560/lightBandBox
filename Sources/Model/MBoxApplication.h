#pragma once

#include "../Audio/MBoxAudioEngine.h"
#include "../Communication/MBoxComServer.h"
#include "../Persistence/MBoxStore.h"
#include "MBoxPlaylist.h"

#include <juce_core/juce_core.h>

#include <memory>
#include <vector>

class MBoxApplication final
{
public:
    explicit MBoxApplication (const juce::File& audioFolder)
        : m_audioEngine (audioFolder), m_store (audioFolder), m_comServer (std::make_unique<MBoxComServer> (*this)) {}
    ~MBoxApplication() = default;

    /** Loads all the playlists from the audio folder and selects the first song of the first playlist.
        Call once at startup: it replaces the playlists, so a previously selected song would be destroyed. */
    void loadPlaylists();

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
    //void testApplication();

private:
    MBoxAudioEngine m_audioEngine;
    MBoxStore m_store;
    std::vector<std::unique_ptr<MBoxPlaylist>> m_playlists;

    // Declared last: destroyed first, so no client request can reach a half-destroyed application.
    std::unique_ptr<MBoxComServer> m_comServer;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MBoxApplication)
};
