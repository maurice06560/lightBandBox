#include "MBoxApplication.h"

#include <algorithm>

juce::StringArray MBoxApplication::getPlaylistNames() const
{
    juce::StringArray names;
    names.ensureStorageAllocated ((int) m_playlists.size());

    for (const auto& playlist : m_playlists)
        names.add (playlist->getName());

    return names;
}

juce::StringArray MBoxApplication::getSongNames (const juce::String& playlistName) const
{
    if (const auto* playlist = getPlaylist (playlistName))
        return playlist->getSongs();

    return {};
}

bool MBoxApplication::addPlaylist (const juce::String& name)
{
    if (getPlaylist (name) != nullptr)
        return false;

    m_playlists.push_back (std::make_unique<MBoxPlaylist> (name));
    return true;
}

bool MBoxApplication::addSong (const juce::String& playlistName, const juce::String& songName)
{
    if (auto* playlist = getPlaylist (playlistName))
        return playlist->addSong (songName);

    return false;
}

MBoxPlaylist* MBoxApplication::getPlaylist (const juce::String& playlistName) const
{
    const auto it = std::find_if (m_playlists.begin(), m_playlists.end(),
                                  [&playlistName] (const auto& playlist) { return playlist->getName() == playlistName; });

    return it != m_playlists.end() ? it->get() : nullptr;
}

MBoxSong* MBoxApplication::getSong (const juce::String& playlistName, const juce::String& songName) const
{
    if (const auto* playlist = getPlaylist (playlistName))
        return playlist->getSong (songName);

    return nullptr;
}

void MBoxApplication::selectSong (const juce::String& playlistName, const juce::String& song)
{
    const auto* selectedSong = getSong (playlistName, song);

    if (selectedSong == nullptr)
    {
        juce::Logger::writeToLog ("MBoxApplication: song '" + song + "' not found in playlist '" + playlistName + "'");
        return;
    }

    m_audioEngine.selectSong (playlistName, selectedSong);
}

void MBoxApplication::play()
{
    m_audioEngine.play();
}

void MBoxApplication::pause()
{
    m_audioEngine.pause();
}

void MBoxApplication::stop()
{
    m_audioEngine.stop();
}

// =================================================================================
void MBoxApplication::testApplication()
{
    const juce::String playlistName ("SIR Practice with band");
    const juce::String songName ("Secrets");

    addPlaylist (playlistName);
    addSong (playlistName, songName);

    if (auto* song = getSong (playlistName, songName)) {
        song->addAudioTrack (Bar { 1, 1, 1, 0 }, "Secrets - synth - 44100.wav");
        song->setSongStartPosition(Bar{1,1,1,0});
        song->setSongEndPosition(Bar{8,1,1,0});
    }
    selectSong(playlistName, songName);

    play();
}