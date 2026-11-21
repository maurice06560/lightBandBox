#include "MBoxPlaylist.h"

//==============================================================================
// Name validation
//==============================================================================
juce::Result validateItemName (const juce::String& name)
{
    if (name.isEmpty())
        return juce::Result::fail ("A name can't be empty");

    if (name.length() > 64)
        return juce::Result::fail ("A name can have at most 64 characters");

    if (name != name.trim())
        return juce::Result::fail ("A name can't start or end with a space");

    if (name.endsWithChar ('.'))
        return juce::Result::fail ("A name can't end with a dot");

    if (name.containsAnyOf ("<>:\"/\\|?*"))
        return juce::Result::fail ("A name can't contain any of < > : \" / \\ | ? *");

    for (auto character : name)
        if (character < 32)
            return juce::Result::fail ("A name can't contain control characters");

    static const juce::StringArray reservedNames { "CON", "PRN", "AUX", "NUL",
                                                   "COM1", "COM2", "COM3", "COM4", "COM5", "COM6", "COM7", "COM8", "COM9",
                                                   "LPT1", "LPT2", "LPT3", "LPT4", "LPT5", "LPT6", "LPT7", "LPT8", "LPT9" };

    if (reservedNames.contains (name.upToFirstOccurrenceOf (".", false, false), true))
        return juce::Result::fail ("\"" + name + "\" is a reserved name on Windows");

    return juce::Result::ok();
}

//==============================================================================
// MBoxPlaylist
//==============================================================================
MBoxPlaylist::MBoxPlaylist (const juce::String& name)
    : m_name (name)
{
}

MBoxPlaylist::MBoxPlaylist (const MBoxPlaylist& other)
    : m_name (other.m_name)
{
    for (const auto& song : other.m_songs)
        m_songs.push_back (std::make_unique<MBoxSong> (*song));
}

juce::String MBoxPlaylist::getName() const  { return m_name; }

juce::Result MBoxPlaylist::rename (const juce::String& newName)
{
    const auto result = validateItemName (newName);

    if (result.wasOk())
        m_name = newName;

    return result;
}

juce::Result MBoxPlaylist::addSong (const juce::String& songName)
{
    return addSong (std::make_unique<MBoxSong> (songName));
}

juce::Result MBoxPlaylist::addSong (std::unique_ptr<MBoxSong> song)
{
    const auto result = validateItemName (song->getName());

    if (result.failed())
        return result;

    if (getSongIndex (song->getName()) >= 0)
        return juce::Result::fail ("Playlist " + m_name + " already has a song named " + song->getName());

    m_songs.push_back (std::move (song));
    return juce::Result::ok();
}

juce::Result MBoxPlaylist::removeSong (const juce::String& songName)
{
    const auto index = getSongIndex (songName);

    if (index < 0)
        return juce::Result::fail ("No song " + songName + " in playlist " + m_name);

    m_songs.erase (m_songs.begin() + index);
    return juce::Result::ok();
}

juce::Result MBoxPlaylist::renameSong (const juce::String& songName, const juce::String& newName)
{
    auto* song = getSongInstance (songName);

    if (song == nullptr)
        return juce::Result::fail ("No song " + songName + " in playlist " + m_name);

    const auto result = validateItemName (newName);

    if (result.failed())
        return result;

    // Changing only the case of the name is allowed
    const auto existing = getSongIndex (newName);

    if (existing >= 0 && m_songs[(size_t) existing].get() != song)
        return juce::Result::fail ("Playlist " + m_name + " already has a song named " + newName);

    song->m_name = newName;
    return juce::Result::ok();
}

juce::Result MBoxPlaylist::moveSong (const juce::String& songName, int to)
{
    const auto from = getSongIndex (songName);

    if (from < 0)
        return juce::Result::fail ("No song " + songName + " in playlist " + m_name);

    if (to < 0 || to >= (int) m_songs.size())
        return juce::Result::fail ("Position " + juce::String (to) + " is outside the playlist");

    auto song = std::move (m_songs[(size_t) from]);
    m_songs.erase (m_songs.begin() + from);
    m_songs.insert (m_songs.begin() + to, std::move (song));
    return juce::Result::ok();
}

MBoxSong* MBoxPlaylist::getSongInstance (const juce::String& songName) const
{
    const auto index = getSongIndex (songName);
    return index >= 0 ? m_songs[(size_t) index].get() : nullptr;
}

int MBoxPlaylist::getSongIndex (const juce::String& songName) const
{
    for (size_t i = 0; i < m_songs.size(); ++i)
        if (m_songs[i]->getName().equalsIgnoreCase (songName))
            return (int) i;

    return -1;
}

juce::StringArray MBoxPlaylist::getSongNames() const
{
    juce::StringArray names;

    for (const auto& song : m_songs)
        names.add (song->getName());

    return names;
}
