#include "MBoxStorage.h"

namespace
{
    const juce::Identifier idSchemaVersion ("schemaVersion");

    juce::var makeObject()  { return juce::var (new juce::DynamicObject()); }

    void set (juce::var& object, const juce::Identifier& name, const juce::var& value)
    {
        object.getDynamicObject()->setProperty (name, value);
    }

    // Reads an optional property, falling back to a default when it's missing
    juce::var get (const juce::var& object, const char* name, const juce::var& fallback = {})
    {
        return object.hasProperty (name) ? object[name] : fallback;
    }

    juce::Result parsePosition (const juce::var& value, Bar& position, const juce::String& context)
    {
        if (! Bar::fromString (value.toString().toStdString(), position))
            return juce::Result::fail (context + ": invalid position \"" + value.toString() + "\"");

        return juce::Result::ok();
    }

    juce::var positionToJson (const Bar& position)  { return juce::String (position.toString()); }

    // Stores a path relative to base when the file is inside it, so a data folder can be moved
    juce::String toStoredPath (const juce::File& file, const juce::File& base)
    {
        if (file == juce::File())
            return {};

        return file.isAChildOf (base) ? file.getRelativePathFrom (base).replaceCharacter ('\\', '/')
                                      : file.getFullPathName();
    }

    juce::File fromStoredPath (const juce::String& path, const juce::File& base)
    {
        if (path.isEmpty())
            return {};

        return juce::File::isAbsolutePath (path) ? juce::File (path) : base.getChildFile (path);
    }
}

//==============================================================================
// Locations
//==============================================================================
MBoxStorage::MBoxStorage (const juce::File& root)
    : m_root (root)
{
}

juce::File MBoxStorage::getDefaultDataDirectory()
{
    const auto fromEnvironment = juce::SystemStats::getEnvironmentVariable ("ONE_BOX_DATA_DIR", {});

    if (fromEnvironment.isNotEmpty())
        return juce::File (fromEnvironment);

    return juce::File::getSpecialLocation (juce::File::userHomeDirectory).getChildFile ("OneBox");
}

juce::File MBoxStorage::getPlaylistDirectory (const juce::String& playlist) const
{
    return m_root.getChildFile ("playlists").getChildFile (playlist);
}

juce::File MBoxStorage::getSongDirectory (const juce::String& playlist, const juce::String& song) const
{
    return getPlaylistDirectory (playlist).getChildFile ("songs").getChildFile (song);
}

juce::File MBoxStorage::getTracksDirectory (const juce::String& playlist, const juce::String& song) const
{
    return getSongDirectory (playlist, song).getChildFile ("tracks");
}

juce::File MBoxStorage::getMediaDirectory (const juce::String& playlist, const juce::String& song) const
{
    return getTracksDirectory (playlist, song).getChildFile ("media");
}

//==============================================================================
// File reading and writing
//==============================================================================
juce::Result MBoxStorage::writeJsonFile (const juce::File& file, const juce::var& json)
{
    if (auto result = file.getParentDirectory().createDirectory(); result.failed())
        return juce::Result::fail ("Can't create " + file.getParentDirectory().getFullPathName() + ": " + result.getErrorMessage());

    juce::TemporaryFile temporary (file);

    if (! temporary.getFile().replaceWithText (juce::JSON::toString (json)))
        return juce::Result::fail ("Can't write " + temporary.getFile().getFullPathName());

    if (! temporary.overwriteTargetFileWithTemporary())
        return juce::Result::fail ("Can't replace " + file.getFullPathName());

    return juce::Result::ok();
}

juce::Result MBoxStorage::readJsonFile (const juce::File& file, juce::var& json)
{
    json = juce::var();

    if (! file.existsAsFile())
        return juce::Result::ok();

    const auto result = juce::JSON::parse (file.loadFileAsString(), json);

    if (result.failed())
        return juce::Result::fail (file.getFullPathName() + ": " + result.getErrorMessage());

    if (! json.isObject())
        return juce::Result::fail (file.getFullPathName() + ": expected a JSON object");

    const int version = json.getProperty (idSchemaVersion, 0);

    if (version < 1 || version > schemaVersion)
        return juce::Result::fail (file.getFullPathName() + ": unsupported schema version " + juce::String (version));

    return juce::Result::ok();
}

//==============================================================================
// Effects and banks
//==============================================================================
juce::var MBoxStorage::effectsToJson (const std::vector<MBoxEffect>& effects)
{
    juce::Array<juce::var> list;

    for (const auto& effect : effects)
    {
        auto json = makeObject();
        set (json, "name", effect.m_name);
        set (json, "format", effect.m_format);
        set (json, "manufacturer", effect.m_manufacturer);
        set (json, "identifier", effect.m_identifier);
        set (json, "uniqueId", effect.m_uniqueId);
        set (json, "version", effect.m_version);
        set (json, "state", effect.m_state.toBase64Encoding());
        set (json, "bypassed", effect.m_bypassed);
        list.add (json);
    }

    return list;
}

juce::Result MBoxStorage::effectsFromJson (const juce::var& json, std::vector<MBoxEffect>& effects)
{
    effects.clear();

    if (json.isVoid())
        return juce::Result::ok();

    if (! json.isArray())
        return juce::Result::fail ("Effects: expected a list");

    for (const auto& item : *json.getArray())
    {
        MBoxEffect effect;
        effect.m_name         = get (item, "name").toString();
        effect.m_format       = get (item, "format").toString();
        effect.m_manufacturer = get (item, "manufacturer").toString();
        effect.m_identifier   = get (item, "identifier").toString();
        effect.m_uniqueId     = get (item, "uniqueId", 0);
        effect.m_version      = get (item, "version").toString();
        effect.m_bypassed     = get (item, "bypassed", false);

        if (! effect.m_state.fromBase64Encoding (get (item, "state").toString()))
            return juce::Result::fail ("Effect " + effect.m_name + ": invalid state data");

        effects.push_back (std::move (effect));
    }

    return juce::Result::ok();
}

juce::var MBoxStorage::banksToJson (const MBoxBanks& banks)
{
    auto json = makeObject();
    set (json, idSchemaVersion, schemaVersion);
    juce::Array<juce::var> bankList;

    for (const auto& bank : banks)
    {
        auto bankJson = makeObject();
        set (bankJson, "name", bank.m_name);
        juce::Array<juce::var> slotList;

        for (const auto& slot : bank.m_bankSlots)
        {
            auto slotJson = makeObject();
            set (slotJson, "name", slot.m_name);
            set (slotJson, "effects", effectsToJson (slot.m_effectChain));
            slotList.add (slotJson);
        }

        set (bankJson, "slots", slotList);
        bankList.add (bankJson);
    }

    set (json, "banks", bankList);
    return json;
}

juce::Result MBoxStorage::banksFromJson (const juce::var& json, MBoxBanks& banks)
{
    banks = createDefaultBanks();
    const auto bankList = get (json, "banks");

    if (bankList.isVoid())
        return juce::Result::ok();

    if (! bankList.isArray() || bankList.size() > numberOfBanks)
        return juce::Result::fail ("banks.json: expected a list of at most " + juce::String (numberOfBanks) + " banks");

    for (int b = 0; b < bankList.size(); ++b)
    {
        auto& bank = banks[(size_t) b];
        bank.m_name = get (bankList[b], "name", bank.m_name).toString();
        const auto slotList = get (bankList[b], "slots");

        if (slotList.isVoid())
            continue;

        if (! slotList.isArray() || slotList.size() > numberOfSlotsPerBanks)
            return juce::Result::fail ("banks.json: bank " + juce::String (b + 1) + " has too many slots");

        for (int s = 0; s < slotList.size(); ++s)
        {
            auto& slot = bank.m_bankSlots[(size_t) s];
            slot.m_name = get (slotList[s], "name", slot.m_name).toString();

            if (auto result = effectsFromJson (get (slotList[s], "effects"), slot.m_effectChain); result.failed())
                return juce::Result::fail ("banks.json: bank " + juce::String (b + 1) + ", slot " + juce::String (s + 1) + ": " + result.getErrorMessage());
        }
    }

    return juce::Result::ok();
}

//==============================================================================
// Rig
//==============================================================================
juce::var MBoxStorage::rigToJson (const MBoxRig& rig)
{
    auto json = makeObject();
    set (json, idSchemaVersion, schemaVersion);

    auto device = makeObject();
    set (device, "type", rig.m_device.m_typeName);
    set (device, "input", rig.m_device.m_inputDeviceName);
    set (device, "output", rig.m_device.m_outputDeviceName);
    set (device, "bufferSize", rig.m_device.m_bufferSize);
    set (json, "device", device);

    juce::Array<juce::var> buses;

    for (const auto& bus : rig.m_mixBuses)
    {
        auto busJson = makeObject();
        set (busJson, "id", bus.getId());
        set (busJson, "name", bus.getName());
        set (busJson, "outputs", juce::Array<juce::var> { bus.getOutputs().first, bus.getOutputs().second });
        set (busJson, "masterVolume", bus.getMasterVolume());
        set (busJson, "masterBalance", bus.getMasterBalance());
        set (busJson, "metronomeVolume", bus.getMetronomeVolume());
        set (busJson, "metronomeOn", bus.isMetronomeOn());
        set (busJson, "masterEffects", effectsToJson (bus.getMasterEffectChain()));
        buses.add (busJson);
    }

    set (json, "mixBuses", buses);
    return json;
}

juce::Result MBoxStorage::rigFromJson (const juce::var& json, MBoxRig& rig)
{
    rig = MBoxRig();

    if (json.isVoid())
        return juce::Result::ok();

    const auto device = get (json, "device");
    rig.m_device.m_typeName         = get (device, "type").toString();
    rig.m_device.m_inputDeviceName  = get (device, "input").toString();
    rig.m_device.m_outputDeviceName = get (device, "output").toString();
    rig.m_device.m_bufferSize       = get (device, "bufferSize", 0);

    const auto buses = get (json, "mixBuses");

    if (buses.isVoid())
        return juce::Result::ok();

    if (! buses.isArray())
        return juce::Result::fail ("rig.json: mixBuses must be a list");

    for (const auto& busJson : *buses.getArray())
    {
        const int id = get (busJson, "id", 0);

        if (id < 1 || id > numberOfMixBus)
            return juce::Result::fail ("rig.json: invalid mix bus id " + juce::String (id));

        auto& bus = rig.m_mixBuses[(size_t) id - 1];
        bus.rename (get (busJson, "name", bus.getName()).toString());

        const auto outputs = get (busJson, "outputs");

        if (outputs.isArray() && outputs.size() == 2 && ! bus.setOutputs ({ (int) outputs[0], (int) outputs[1] }))
            return juce::Result::fail ("rig.json: invalid outputs for mix bus " + juce::String (id));

        bus.setMasterVolume (get (busJson, "masterVolume", 0.0));
        bus.setMasterBalance (get (busJson, "masterBalance", 0.0));
        bus.setMetronomeVolume (get (busJson, "metronomeVolume", 0.0));
        bus.setMetronomeOn (get (busJson, "metronomeOn", true));

        if (auto result = effectsFromJson (get (busJson, "masterEffects"), bus.getMasterEffectChain()); result.failed())
            return juce::Result::fail ("rig.json: mix bus " + juce::String (id) + ": " + result.getErrorMessage());
    }

    return juce::Result::ok();
}

//==============================================================================
// Song
//==============================================================================
juce::var MBoxStorage::songToJson (const MBoxSong& song)
{
    auto json = makeObject();
    set (json, idSchemaVersion, schemaVersion);
    set (json, "name", song.getName());
    set (json, "totalBars", song.getTotalBar());
    set (json, "leftLocator", positionToJson (song.getLeftLocator()));
    set (json, "rightLocator", positionToJson (song.getRightLocator()));
    set (json, "endSongAction", juce::String (toString (song.getEndSongAction())));
    set (json, "endSongDelay", song.getEndSongDelay());
    set (json, "metronome", juce::String (toString (song.getMetronomeState())));

    juce::Array<juce::var> tempos;

    for (const auto& [position, bpm] : song.getTempoGrid())
    {
        auto entry = makeObject();
        set (entry, "position", positionToJson (position));
        set (entry, "bpm", bpm);
        tempos.add (entry);
    }

    set (json, "tempos", tempos);

    juce::Array<juce::var> signatures;

    for (const auto& [position, signature] : song.getSignatureGrid())
    {
        auto entry = makeObject();
        set (entry, "position", positionToJson (position));
        set (entry, "numerator", signature.m_numerator);
        set (entry, "denominator", signature.m_denominator);
        signatures.add (entry);
    }

    set (json, "signatures", signatures);

    juce::Array<juce::var> mixes;

    for (int bus = 1; bus <= numberOfMixBus; ++bus)
    {
        auto mix = makeObject();
        set (mix, "bus", bus);
        juce::Array<juce::var> tracks;

        for (const auto& trackMix : song.getMix (bus))
        {
            auto entry = makeObject();
            set (entry, "trackId", trackMix.m_trackId);
            set (entry, "volume", trackMix.m_volume);
            set (entry, "balance", trackMix.m_balance);
            set (entry, "mute", trackMix.m_mute);
            set (entry, "solo", trackMix.m_solo);
            set (entry, "inserts", effectsToJson (trackMix.m_inserts));
            tracks.add (entry);
        }

        set (mix, "tracks", tracks);
        mixes.add (mix);
    }

    set (json, "mixes", mixes);
    return json;
}

juce::Result MBoxStorage::songFromJson (const juce::var& json, MBoxSong& song)
{
    const auto context = "Song " + song.getName();

    song.setTotalBar (get (json, "totalBars", defaultTotalBar));
    song.setEndSongDelay (get (json, "endSongDelay", 0));

    EndSongAction action = EndSongAction::Stop;
    if (! fromString (get (json, "endSongAction", "stop").toString().toStdString(), action))
        return juce::Result::fail (context + ": invalid endSongAction");
    song.setEndSongAction (action);

    MetronomeState metronome = MetronomeState::Off;
    if (! fromString (get (json, "metronome", "off").toString().toStdString(), metronome))
        return juce::Result::fail (context + ": invalid metronome state");
    song.setMetronomeState (metronome);

    std::vector<std::pair<Bar, int>> tempoGrid;
    std::vector<std::pair<Bar, Signature>> signatureGrid;

    if (const auto tempos = get (json, "tempos"); tempos.isArray())
    {
        for (const auto& entry : *tempos.getArray())
        {
            Bar position;
            if (auto result = parsePosition (get (entry, "position"), position, context); result.failed())
                return result;
            tempoGrid.push_back ({ position, (int) get (entry, "bpm", defaultTempo) });
        }
    }

    if (const auto signatures = get (json, "signatures"); signatures.isArray())
    {
        for (const auto& entry : *signatures.getArray())
        {
            Bar position;
            if (auto result = parsePosition (get (entry, "position"), position, context); result.failed())
                return result;
            signatureGrid.push_back ({ position, Signature { get (entry, "numerator", 4), get (entry, "denominator", 4) } });
        }
    }

    song.setGridsForStorage (std::move (tempoGrid), std::move (signatureGrid));

    Bar left, right { defaultTotalBar, 1, 1, 0 };
    if (auto result = parsePosition (get (json, "leftLocator", "1.1.1.0"), left, context); result.failed())
        return result;
    if (auto result = parsePosition (get (json, "rightLocator", juce::String (right.toString())), right, context); result.failed())
        return result;
    song.setLocatorsForStorage (left, right);

    if (const auto mixes = get (json, "mixes"); mixes.isArray())
    {
        for (const auto& mix : *mixes.getArray())
        {
            const int bus = get (mix, "bus", 0);

            if (bus < 1 || bus > numberOfMixBus)
                return juce::Result::fail (context + ": invalid mix bus " + juce::String (bus));

            auto& trackMixes = song.getMix (bus);
            trackMixes.clear();

            if (const auto tracks = get (mix, "tracks"); tracks.isArray())
            {
                for (const auto& entry : *tracks.getArray())
                {
                    MBoxTrackMix trackMix;
                    trackMix.m_trackId = get (entry, "trackId", 0);
                    trackMix.m_volume  = juce::jmin (maxVolumeDb, (double) get (entry, "volume", 0.0));
                    trackMix.m_balance = juce::jlimit (-1.0, 1.0, (double) get (entry, "balance", 0.0));
                    trackMix.m_mute    = get (entry, "mute", false);
                    trackMix.m_solo    = get (entry, "solo", false);

                    if (auto result = effectsFromJson (get (entry, "inserts"), trackMix.m_inserts); result.failed())
                        return juce::Result::fail (context + ": " + result.getErrorMessage());

                    trackMixes.push_back (std::move (trackMix));
                }
            }
        }
    }

    return juce::Result::ok();
}

//==============================================================================
// Tracks
//==============================================================================
juce::var MBoxStorage::tracksToJson (const MBoxSong& song, const juce::File& tracksDirectory)
{
    auto json = makeObject();
    set (json, idSchemaVersion, schemaVersion);
    juce::Array<juce::var> tracks;

    for (const auto& track : song.getTracks())
    {
        auto entry = makeObject();
        set (entry, "id", track->getId());
        set (entry, "name", track->getName());
        set (entry, "type", juce::String (toString (track->getType())));

        if (const auto* audio = dynamic_cast<const MBoxAudioTrack*> (track.get()))
        {
            set (entry, "audioStart", positionToJson (audio->getAudioStart()));
            set (entry, "file", toStoredPath (audio->getFilename(), tracksDirectory));
            set (entry, "lengthInFrames", (juce::int64) audio->getLengthInFrames());
        }
        else if (const auto* physical = dynamic_cast<const MBoxPhysicalTrack*> (track.get()))
        {
            set (entry, "physicalInput", physical->getPhysicalInput());
            set (entry, "diOutput", physical->getDIOutput());
            juce::Array<juce::var> changes;

            for (const auto& [position, slot] : physical->getBankSlotChanges())
            {
                auto change = makeObject();
                set (change, "position", positionToJson (position));
                set (change, "bank", slot.m_bank);
                set (change, "slot", slot.m_slot);
                changes.add (change);
            }

            set (entry, "bankSlots", changes);
        }
        else if (const auto* info = dynamic_cast<const MBoxInfoTrack*> (track.get()))
        {
            juce::Array<juce::var> clicks;

            for (const auto& position : info->getInfoClicks())
                clicks.add (positionToJson (position));

            set (entry, "infoClicks", clicks);
        }

        tracks.add (entry);
    }

    set (json, "tracks", tracks);
    return json;
}

juce::Result MBoxStorage::tracksFromJson (const juce::var& json, MBoxSong& song, const juce::File& tracksDirectory)
{
    const auto tracks = get (json, "tracks");

    if (tracks.isVoid())
        return juce::Result::ok();

    if (! tracks.isArray())
        return juce::Result::fail ("Song " + song.getName() + ": tracks must be a list");

    for (const auto& entry : *tracks.getArray())
    {
        const int id = get (entry, "id", 0);
        const auto name = get (entry, "name").toString();
        const auto context = "Song " + song.getName() + ", track " + juce::String (id);

        TrackType type = TrackType::Audio;
        if (id < 1 || ! fromString (get (entry, "type").toString().toStdString(), type))
            return juce::Result::fail (context + ": invalid id or type");

        if (type == TrackType::Audio)
        {
            auto audio = std::make_unique<MBoxAudioTrack> (id, name);
            Bar start;
            if (auto result = parsePosition (get (entry, "audioStart", "1.1.1.0"), start, context); result.failed())
                return result;
            audio->setAudioStart (start);
            audio->setFilename (fromStoredPath (get (entry, "file").toString(), tracksDirectory),
                                (juce::int64) get (entry, "lengthInFrames", 0));
            song.addTrackForStorage (std::move (audio));
        }
        else if (type == TrackType::Physical)
        {
            auto physical = std::make_unique<MBoxPhysicalTrack> (id, name);
            physical->setPhysicalInput (get (entry, "physicalInput", 0));
            physical->setDIOutput (get (entry, "diOutput", -1));

            if (const auto changes = get (entry, "bankSlots"); changes.isArray())
            {
                for (const auto& change : *changes.getArray())
                {
                    Bar position;
                    if (auto result = parsePosition (get (change, "position"), position, context); result.failed())
                        return result;

                    const MBoxSlotRef slot { get (change, "bank", 1), get (change, "slot", 1) };

                    if (! slot.isValid())
                        return juce::Result::fail (context + ": invalid bank slot");

                    physical->setBankSlotChange (position, slot);
                }
            }

            song.addTrackForStorage (std::move (physical));
        }
        else
        {
            auto info = std::make_unique<MBoxInfoTrack> (id, name);

            if (const auto clicks = get (entry, "infoClicks"); clicks.isArray())
            {
                for (const auto& click : *clicks.getArray())
                {
                    Bar position;
                    if (auto result = parsePosition (click, position, context); result.failed())
                        return result;
                    info->addInfoClick (position);
                }
            }

            song.addTrackForStorage (std::move (info));
        }
    }

    return juce::Result::ok();
}

//==============================================================================
// Whole data folder
//==============================================================================
juce::Result MBoxStorage::load (MBoxPlaylists& playlists, MBoxRig& rig, MBoxBanks& banks) const
{
    playlists.clear();
    juce::var json;

    if (auto result = readJsonFile (m_root.getChildFile ("banks.json"), json); result.failed())
        return result;
    if (auto result = banksFromJson (json, banks); result.failed())
        return result;

    if (auto result = readJsonFile (m_root.getChildFile ("rig.json"), json); result.failed())
        return result;
    if (auto result = rigFromJson (json, rig); result.failed())
        return result;

    if (auto result = readJsonFile (m_root.getChildFile ("playlists").getChildFile ("playlists.json"), json); result.failed())
        return result;

    const auto playlistList = get (json, "playlists");

    if (! playlistList.isArray())
        return juce::Result::ok();

    for (const auto& playlistJson : *playlistList.getArray())
    {
        auto playlist = std::make_unique<MBoxPlaylist> (get (playlistJson, "name").toString());

        if (auto result = validateItemName (playlist->getName()); result.failed())
            return juce::Result::fail ("playlists.json: " + result.getErrorMessage());

        if (const auto songNames = get (playlistJson, "songs"); songNames.isArray())
        {
            for (const auto& songName : *songNames.getArray())
            {
                auto song = std::make_unique<MBoxSong> (songName.toString());
                juce::var songJson, tracksJson;

                if (auto result = readJsonFile (getSongDirectory (playlist->getName(), song->getName()).getChildFile ("song.json"), songJson); result.failed())
                    return result;
                if (auto result = songFromJson (songJson, *song); result.failed())
                    return result;

                const auto tracksDirectory = getTracksDirectory (playlist->getName(), song->getName());

                if (auto result = readJsonFile (tracksDirectory.getChildFile ("track.json"), tracksJson); result.failed())
                    return result;
                if (auto result = tracksFromJson (tracksJson, *song, tracksDirectory); result.failed())
                    return result;
                if (auto result = song->validateAfterLoad(); result.failed())
                    return result;
                if (auto result = playlist->addSong (std::move (song)); result.failed())
                    return juce::Result::fail ("Playlist " + playlist->getName() + ": " + result.getErrorMessage());
            }
        }

        playlists.push_back (std::move (playlist));
    }

    return juce::Result::ok();
}

juce::Result MBoxStorage::save (const MBoxPlaylists& playlists, const MBoxRig& rig, const MBoxBanks& banks) const
{
    if (auto result = writeJsonFile (m_root.getChildFile ("banks.json"), banksToJson (banks)); result.failed())
        return result;

    if (auto result = writeJsonFile (m_root.getChildFile ("rig.json"), rigToJson (rig)); result.failed())
        return result;

    auto index = makeObject();
    set (index, idSchemaVersion, schemaVersion);
    juce::Array<juce::var> playlistList;

    for (const auto& playlist : playlists)
    {
        auto playlistJson = makeObject();
        set (playlistJson, "name", playlist->getName());
        juce::Array<juce::var> songNames;

        for (const auto& song : playlist->getSongs())
        {
            songNames.add (song->getName());
            const auto tracksDirectory = getTracksDirectory (playlist->getName(), song->getName());

            if (auto result = writeJsonFile (getSongDirectory (playlist->getName(), song->getName()).getChildFile ("song.json"), songToJson (*song)); result.failed())
                return result;
            if (auto result = writeJsonFile (tracksDirectory.getChildFile ("track.json"), tracksToJson (*song, tracksDirectory)); result.failed())
                return result;
            if (auto result = tracksDirectory.getChildFile ("media").createDirectory(); result.failed())
                return result;
        }

        set (playlistJson, "songs", songNames);
        playlistList.add (playlistJson);
    }

    set (index, "playlists", playlistList);
    return writeJsonFile (m_root.getChildFile ("playlists").getChildFile ("playlists.json"), index);
}

//==============================================================================
// Folder operations
//==============================================================================
namespace
{
    juce::Result renameDirectory (const juce::File& from, const juce::File& to)
    {
        if (! from.isDirectory())
            return juce::Result::ok();

        // A name differing only by case is the same folder on macOS and Windows
        if (to.exists() && ! from.getFullPathName().equalsIgnoreCase (to.getFullPathName()))
            return juce::Result::fail ("A folder already exists at " + to.getFullPathName());

        if (! from.moveFileTo (to))
            return juce::Result::fail ("Can't rename " + from.getFullPathName() + " to " + to.getFileName());

        return juce::Result::ok();
    }
}

juce::Result MBoxStorage::renamePlaylistDirectory (const juce::String& oldName, const juce::String& newName) const
{
    return renameDirectory (getPlaylistDirectory (oldName), getPlaylistDirectory (newName));
}

juce::Result MBoxStorage::renameSongDirectory (const juce::String& playlist, const juce::String& oldName, const juce::String& newName) const
{
    return renameDirectory (getSongDirectory (playlist, oldName), getSongDirectory (playlist, newName));
}

juce::Result MBoxStorage::moveToTrash (const juce::File& directory, const juce::String& label) const
{
    if (! directory.isDirectory())
        return juce::Result::ok();

    const auto trash = m_root.getChildFile (".trash");

    if (auto result = trash.createDirectory(); result.failed())
        return result;

    const auto target = trash.getNonexistentChildFile (juce::Time::getCurrentTime().formatted ("%Y%m%d-%H%M%S-") + label, {}, false);

    if (! directory.moveFileTo (target))
        return juce::Result::fail ("Can't move " + directory.getFullPathName() + " to the trash");

    return juce::Result::ok();
}

juce::Result MBoxStorage::trashPlaylistDirectory (const juce::String& playlist) const
{
    return moveToTrash (getPlaylistDirectory (playlist), playlist);
}

juce::Result MBoxStorage::trashSongDirectory (const juce::String& playlist, const juce::String& song) const
{
    return moveToTrash (getSongDirectory (playlist, song), playlist + " - " + song);
}
