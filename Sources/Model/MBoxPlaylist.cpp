#include "MBoxPlaylist.h"

#include <algorithm>

MBoxPlaylist::MBoxPlaylist (const juce::String& name)
    : m_name (name)
{
}

const juce::String& MBoxPlaylist::getName() const
{
    return m_name;
}

juce::StringArray MBoxPlaylist::getSongs() const
{
    juce::StringArray names;
    names.ensureStorageAllocated ((int) m_songs.size());

    for (const auto& song : m_songs)
        names.add (song->getName());

    return names;
}

MBoxSong* MBoxPlaylist::getSong (const juce::String& songName) const
{
    const auto it = std::find_if (m_songs.begin(), m_songs.end(),
                                  [&songName] (const auto& song) { return song->getName() == songName; });

    return it != m_songs.end() ? it->get() : nullptr;
}

bool MBoxPlaylist::addSong (const juce::String& songName)
{
    if (getSong (songName) != nullptr)
        return false;

    m_songs.push_back (std::make_unique<MBoxSong> (songName));
    return true;
}
