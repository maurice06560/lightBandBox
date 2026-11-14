#include "MBoxStore.h"

namespace
{
    void logError (const juce::String& message)
    {
        juce::Logger::writeToLog ("MBoxStore: ERROR - " + message);
    }

    /** Parses a JSON file. Returns a void var (and logs) if the file is missing or invalid. */
    juce::var readJson (const juce::File& file)
    {
        if (! file.existsAsFile())
        {
            logError ("file not found: " + file.getFullPathName());
            return {};
        }

        juce::var json;
        const auto result = juce::JSON::parse (file.loadFileAsString(), json);

        if (result.failed())
        {
            logError ("invalid JSON in " + file.getFullPathName() + ": " + result.getErrorMessage());
            return {};
        }

        return json;
    }

    /** Reads a { "bar", "beat", "division", "tick" } object. Missing fields keep the Bar defaults. */
    Bar parseBar (const juce::var& position)
    {
        Bar bar;
        bar.bar      = position.getProperty ("bar", bar.bar);
        bar.beat     = position.getProperty ("beat", bar.beat);
        bar.division = position.getProperty ("division", bar.division);
        bar.tick     = position.getProperty ("tick", bar.tick);
        return bar;
    }

    /** Returns the array stored at key, or an empty array. */
    const juce::Array<juce::var>& getArray (const juce::var& object, const juce::Identifier& key)
    {
        static const juce::Array<juce::var> empty;

        if (const auto* array = object.getProperty (key, {}).getArray())
            return *array;

        return empty;
    }
}

std::vector<std::unique_ptr<MBoxPlaylist>> MBoxStore::loadPlaylists() const
{
    std::vector<std::unique_ptr<MBoxPlaylist>> playlists;

    const auto playlistsFolder = getPlaylistsFolder();
    const auto json = readJson (playlistsFolder.getChildFile ("playlists.json"));

    for (const auto& playlistJson : getArray (json, "playlists"))
    {
        const auto playlistName = playlistJson.getProperty ("name", {}).toString();

        if (playlistName.isEmpty())
        {
            logError ("playlists.json: playlist without a name, skipped");
            continue;
        }

        //juce::Logger::writeToLog ("MBoxStore: load playlist: " + playlistName);

        auto playlist = std::make_unique<MBoxPlaylist> (playlistName);
        const auto songsFolder = playlistsFolder.getChildFile (playlistName).getChildFile ("songs");

        for (const auto& songNameVar : getArray (playlistJson, "songs"))
        {
            const auto songName = songNameVar.toString();

            //juce::Logger::writeToLog ("MBoxStore: load songName: " + songName);

            if (! playlist->addSong (songName))
            {
                logError ("playlist '" + playlistName + "': duplicate song '" + songName + "', skipped");
                continue;
            }

            loadSong (songsFolder.getChildFile (songName), *playlist->getSong (songName));
        }

        playlists.push_back (std::move (playlist));
    }

    juce::Logger::writeToLog ("MBoxStore: loaded " + juce::String ((int) playlists.size()) + " playlist(s) from "
                              + playlistsFolder.getFullPathName());
    return playlists;
}

void MBoxStore::loadSong (const juce::File& songFolder, MBoxSong& song) const
{
    const auto json = readJson (songFolder.getChildFile ("song.json"));

    if (json.isVoid())
        return;

    // The grids come first: the track and locator positions are converted with them
    for (const auto& tempo : getArray (json, "tempoGrid"))
        song.addTempo (parseBar (tempo["position"]), juce::roundToInt ((double) tempo.getProperty ("bpm", 120)));

    for (const auto& signature : getArray (json, "signatureGrid"))
        song.addSignature (parseBar (signature["position"]),
                           Signature { signature.getProperty ("numerator", 4), signature.getProperty ("denominator", 4) });

    // Only the first mix is used
    const auto& mixes = getArray (json, "mixes");
    loadTracks (songFolder.getChildFile ("tracks"), mixes.isEmpty() ? juce::var() : mixes.getFirst(), song);

    // Set the locators last: adding a track can move the end position to the end of its audio file
    song.setSongStartPosition (Bar { (int) json.getProperty ("startBar", 1), 1, 1, 0 });
    song.setSongEndPosition   (Bar { (int) json.getProperty ("totalBars", 1), 1, 1, 0 });
}

void MBoxStore::loadTracks (const juce::File& tracksFolder, const juce::var& mix, MBoxSong& song) const
{
    // Ids of the tracks in the mix. Without a mix, every track is loaded.
    const auto hasMix = ! mix.isVoid();
    juce::Array<int> mixTrackIds;

    for (const auto& mixTrack : getArray (mix, "tracks"))
        mixTrackIds.add (mixTrack.getProperty ("track", 0));

    const auto json = readJson (tracksFolder.getChildFile ("track.json"));

    if (! json.isArray())
        return;

    for (const auto& track : *json.getArray())
    {
        if (hasMix && ! mixTrackIds.contains (track.getProperty ("id", 0)))
            continue;

        const auto trackType = track.getProperty ("trackType", {}).toString();

        if (trackType == "Wav")
            loadAudioTrack (tracksFolder, track, song);
        else if (trackType == "Physical")
            loadPhysicalTrack (track, song);
        else
            logError ("track " + track.getProperty ("id", 0).toString() + " in " + tracksFolder.getFullPathName()
                      + " has unknown type '" + trackType + "', skipped");
    }
}

void MBoxStore::loadAudioTrack (const juce::File& tracksFolder, const juce::var& track, MBoxSong& song) const
{
    const auto& file = track["file"];
    const auto path = file.getProperty ("path", {}).toString();

    if (path.isEmpty())
    {
        logError ("track " + track.getProperty ("id", 0).toString() + " in " + tracksFolder.getFullPathName()
                  + " has no audio file, skipped");
        return;
    }

    const auto audioFile = tracksFolder.getChildFile (path);

    if (! audioFile.existsAsFile())
        logError ("audio file not found: " + audioFile.getFullPathName());

    song.addAudioTrack (parseBar (file["position"]), audioFile);
}

void MBoxStore::loadPhysicalTrack (const juce::var& track, MBoxSong& song) const
{
    song.addPhysicalTrack (track.getProperty ("physicalInput", 0));
}
