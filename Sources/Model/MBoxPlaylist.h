#pragma once

#include "MBoxSong.h"

#include <juce_core/juce_core.h>

#include <memory>
#include <vector>

class MBoxPlaylist final
{
public:
    MBoxPlaylist() = default;
    explicit MBoxPlaylist (const juce::String& name);
    ~MBoxPlaylist() = default;

    const juce::String& getName() const;
    juce::StringArray getSongs() const;
    /** Returns the song with the given name, or nullptr if it doesn't exist. */
    MBoxSong* getSong (const juce::String& songName) const;

    /** Adds a song with the given name. Returns false (and does nothing) if a song with that name already exists. */
    bool addSong (const juce::String& songName);

private:
    juce::String m_name;
    std::vector<std::unique_ptr<MBoxSong>> m_songs;
};
