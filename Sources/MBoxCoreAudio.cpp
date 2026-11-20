#include "MBoxCoreAudio.h"
#include "Audio/MBoxAudioEngine.h"
#include "Persistence/MBoxStorage.h"
#include "Plugins/MBoxPluginHost.h"
#include "TCPServer/MBoxServer.h"

#include <juce_events/juce_events.h>

#include <map>

#if JUCE_MAC
namespace juce
{
    // Defined in juce_events (juce_MessageManager_mac.mm). JUCE's own app wrapper calls it;
    // a library has to, or [NSApp run] has no application to run.
    void initialiseNSApplication();
}
#endif

namespace
{
    // Only one MBoxCoreAudio per process; InterProcessLock covers other processes
    std::atomic<bool> instanceExists { false };

    constexpr int timerIntervalMs = 50;     // events at most 20 times per second
    constexpr int saveDelayMs = 1000;       // save this long after the last change

    MBoxResult toResult (const juce::Result& result)
    {
        return result.wasOk() ? MBoxResult::ok() : MBoxResult::fail (result.getErrorMessage().toStdString());
    }

    juce::var makeObject()  { return juce::var (new juce::DynamicObject()); }

    void set (juce::var& object, const juce::Identifier& name, const juce::var& value)
    {
        object.getDynamicObject()->setProperty (name, value);
    }

    std::string toCompactJson (const juce::var& value)
    {
        return juce::JSON::toString (value, juce::JSON::FormatOptions {}.withSpacing (juce::JSON::Spacing::none)).toStdString();
    }

    std::vector<std::string> toStdStrings (const juce::StringArray& strings)
    {
        std::vector<std::string> result;

        for (const auto& string : strings)
            result.push_back (string.toStdString());

        return result;
    }

    // After a song or playlist folder is renamed, moves the WAV paths inside it along
    void rebaseAudioFiles (MBoxSong& song, const juce::File& oldDirectory, const juce::File& newDirectory)
    {
        for (const auto& track : song.getTracks())
        {
            auto* audio = dynamic_cast<MBoxAudioTrack*> (track.get());

            if (audio != nullptr && audio->getFilename().isAChildOf (oldDirectory))
                audio->setFilename (newDirectory.getChildFile (audio->getFilename().getRelativePathFrom (oldDirectory)),
                                    audio->getLengthInFrames());
        }
    }
}

//==============================================================================
// The implementation, kept out of the public header so it stays free of JUCE types.
// It holds the members of the diagram (playlists, audio engine, server, storage) plus the
// loading, saving and event state.
//==============================================================================
struct MBoxCoreAudio::Impl final : private juce::Timer
{
    Impl (MBoxCoreAudio& owner, const MBoxCoreOptions& options);
    ~Impl() override;

    // An effect changed over TCP, whose plugin state is saved at the next save
    struct TouchedEffect
    {
        MBoxEffectTarget m_target;
        int m_effectIndex;
        juce::String m_playlist, m_song;  // the song an insert belongs to
    };

    //==========================================================================
    void timerCallback() override;
    void reportStatus();
    void markDirty();
    juce::Result doSave();
    void captureTouchedEffectStates();
    void emit (const char* name, const juce::var& data);

    MBoxPlaylist* findPlaylist (const juce::String& name) const;
    MBoxSong* getSelectedSong() const;
    MBoxSongPlayer* getRuntimeSong (const juce::String& songName) const;
    MBoxRenderer& getRenderer() const  { return m_audioEngine->getRenderer(); }

    void startLoad (const juce::String& playlistName, const juce::String& songName, bool reselect);
    void finishLoad (int generation, std::unique_ptr<MBoxPlaylistPlayer> player, const juce::String& error);
    void rebuildIfLoaded (const juce::String& playlistName);
    void unloadPlaylist();
    void pushBusToRenderer (int bus);

    std::vector<MBoxEffect>* getModelEffects (const MBoxEffectTarget& target, const juce::String& playlist, const juce::String& song);
    std::vector<MBoxEffectChain*> getRuntimeChains (const MBoxEffectTarget& target, const juce::String& song) const;

    MBoxResult changeTrackMix (int bus, int trackId, const std::function<void (MBoxTrackMix&)>& change);
    MBoxResult changeBus (int bus, const std::function<void (MBoxMixBus&)>& change);

    //==========================================================================
    MBoxCoreAudio& m_owner;

    // Starts JUCE's MessageManager, which the AudioDeviceManager needs. Must outlive everything below
    std::unique_ptr<juce::ScopedJuceInitialiser_GUI> m_juceInitialiser;
    std::unique_ptr<juce::InterProcessLock> m_instanceLock;
    bool m_ownsInstance { false };
    juce::StringArray m_initialisationErrors;
    bool m_readOnly { false };  // set when the data folder failed to load, so it's never overwritten

    std::unique_ptr<MBoxStorage> m_storage;
    std::vector<std::unique_ptr<MBoxPlaylist>> m_playlists;
    MBoxRig m_rig;
    MBoxBanks m_banks;
    std::unique_ptr<MBoxPluginHost> m_pluginHost;
    MBoxAudioClipCache m_clipCache;

    // Audio engine
    std::unique_ptr<MBoxAudioEngine> m_audioEngine;
    std::unique_ptr<MBoxServer> m_server;
    juce::ThreadPool m_loader { juce::ThreadPoolOptions {}.withNumberOfThreads (1).withThreadName ("MBox loader") };

    // Selection and loading
    juce::String m_loadedPlaylist, m_selectedSong;
    bool m_loading { false };
    juce::String m_loadingPlaylist, m_loadingSong;
    bool m_reselectAfterLoad { true };
    std::atomic<int> m_loadGeneration { 0 };
    std::atomic<float> m_loadProgress { 0.0f };
    float m_reportedProgress { -1.0f };

    // Saving
    bool m_dirty { false };
    juce::uint32 m_lastChangeMs { 0 };
    std::vector<TouchedEffect> m_touchedEffects;

    // Events
    std::map<int, std::function<void (const MBoxEvent&)>> m_listeners;
    int m_nextListenerId { 1 };
    MBoxRenderer::Status m_lastStatus;
    juce::String m_lastPosition;

    JUCE_DECLARE_WEAK_REFERENCEABLE (Impl)
};

//==============================================================================
// Startup: single instance, data folder, plugin list, audio engine, TCP server
//==============================================================================
MBoxCoreAudio::Impl::Impl (MBoxCoreAudio& owner, const MBoxCoreOptions& options)
    : m_owner (owner),
      m_juceInitialiser (std::make_unique<juce::ScopedJuceInitialiser_GUI>())
{
    // These always exist, so every method works even when startup fails below
    const auto dataDirectory = options.m_dataDirectory.empty() ? MBoxStorage::getDefaultDataDirectory()
                                                               : juce::File (options.m_dataDirectory);
    m_storage = std::make_unique<MBoxStorage> (dataDirectory);
    m_pluginHost = std::make_unique<MBoxPluginHost> (dataDirectory.getChildFile ("plugins.xml"));
    m_audioEngine = std::make_unique<MBoxAudioEngine>();
    m_banks = createDefaultBanks();

    // A second instance stays inert: no data, no device, no server, never saves
    auto failStartup = [this] (const juce::String& message)
    {
        m_initialisationErrors.add (message);
        m_readOnly = true;
        m_audioEngine->startWithoutDevice();
        startTimer (timerIntervalMs);
    };

    if (instanceExists.exchange (true))
    {
        failStartup ("Another MBoxCoreAudio already exists in this process");
        return;
    }

    m_ownsInstance = true;
    m_instanceLock = std::make_unique<juce::InterProcessLock> ("MBoxCoreAudio");

    if (! m_instanceLock->enter (0))
    {
        m_instanceLock.reset();
        failStartup ("Another application is already running MBoxCoreAudio");
        return;
    }

    dataDirectory.createDirectory();

    if (const auto result = m_storage->load (m_playlists, m_rig, m_banks); result.failed())
    {
        m_readOnly = true;
        m_initialisationErrors.add ("Data folder not loaded, changes won't be saved: " + result.getErrorMessage());
        juce::Logger::writeToLog ("MBoxCoreAudio: " + m_initialisationErrors.strings.getLast());
    }

    if (const auto result = m_pluginHost->loadPluginList(); result.failed())
        m_initialisationErrors.add (result.getErrorMessage());

    if (options.m_openAudioDevice)
    {
        if (const auto result = m_audioEngine->initialise (m_rig.m_device); result.failed())
        {
            m_initialisationErrors.add ("Audio device: " + result.getErrorMessage());
            m_audioEngine->startWithoutDevice();
        }
    }
    else
    {
        m_audioEngine->startWithoutDevice();
    }

    for (int bus = 1; bus <= numberOfMixBus; ++bus)
        pushBusToRenderer (bus);

    auto port = options.m_tcpPort;

    if (port < 0)
        port = juce::SystemStats::getEnvironmentVariable ("ONE_BOX_DATA_TCP_PORT", juce::String (MBoxComAPI::defaultPort)).getIntValue();

    if (port > 0)
    {
        m_server = std::make_unique<MBoxServer> (m_owner);

        if (const auto result = m_server->start (port); result.failed())
        {
            m_initialisationErrors.add (result.getErrorMessage());
            m_server.reset();
        }
    }

    startTimer (timerIntervalMs);
}

MBoxCoreAudio::Impl::~Impl()
{
    masterReference.clear();
    stopTimer();

    // Cancel and wait for a playlist load in progress
    ++m_loadGeneration;
    m_loader.removeAllJobs (true, 30000);

    if (m_dirty && ! m_readOnly && m_storage != nullptr)
        doSave();

    // Stop the engine (and the plugins it runs) before shutting JUCE down
    m_server.reset();
    m_audioEngine.reset();
    m_pluginHost.reset();

    if (m_instanceLock != nullptr)
        m_instanceLock->exit();

    if (m_ownsInstance)
        instanceExists = false;
}

//==============================================================================
// Periodic work on the message thread: free retired players, report status, save
//==============================================================================
void MBoxCoreAudio::Impl::timerCallback()
{
    getRenderer().collectGarbage();
    reportStatus();

    if (m_dirty && ! m_readOnly && juce::Time::getMillisecondCounter() - m_lastChangeMs >= (juce::uint32) saveDelayMs)
        doSave();
}

void MBoxCoreAudio::Impl::reportStatus()
{
    if (m_loading)
    {
        const auto progress = m_loadProgress.load();

        if (progress - m_reportedProgress >= 0.02f)
        {
            m_reportedProgress = progress;
            auto data = makeObject();
            set (data, "playlist", m_loadingPlaylist);
            set (data, "progress", progress);
            emit (MBoxComAPI::Event::playlistLoading, data);
        }
    }

    const auto status = getRenderer().getStatus();
    const auto info = m_owner.getTransportInfo();

    // A Continue moved to the next song: follow it
    if (status.m_songIndex != m_lastStatus.m_songIndex && info.m_song != m_selectedSong.toStdString() && ! info.m_song.empty())
    {
        m_selectedSong = info.m_song;
        auto data = makeObject();
        set (data, "playlist", m_loadedPlaylist);
        set (data, "song", m_selectedSong);
        emit (MBoxComAPI::Event::songChanged, data);
    }

    if (status.m_state != m_lastStatus.m_state || status.m_songIndex != m_lastStatus.m_songIndex
        || juce::String (info.m_position) != m_lastPosition)
    {
        m_lastStatus = status;
        m_lastPosition = info.m_position;

        auto data = makeObject();
        set (data, "state", juce::String (info.m_state));
        set (data, "playlist", juce::String (info.m_playlist));
        set (data, "song", juce::String (info.m_song));
        set (data, "position", juce::String (info.m_position));
        set (data, "bpm", info.m_bpm);
        set (data, "numerator", info.m_numerator);
        set (data, "denominator", info.m_denominator);
        emit (MBoxComAPI::Event::transport, data);
    }
}

void MBoxCoreAudio::Impl::emit (const char* name, const juce::var& data)
{
    const MBoxEvent event { name, toCompactJson (data) };

    if (m_server != nullptr)
        m_server->broadcast (event);

    // A listener may remove itself, so iterate over a copy
    const auto listeners = m_listeners;

    for (const auto& [id, listener] : listeners)
        listener (event);
}

//==============================================================================
// Saving
//==============================================================================
void MBoxCoreAudio::Impl::markDirty()
{
    m_dirty = true;
    m_lastChangeMs = juce::Time::getMillisecondCounter();
}

juce::Result MBoxCoreAudio::Impl::doSave()
{
    if (m_readOnly)
        return juce::Result::fail ("The data folder failed to load, so it isn't saved");

    captureTouchedEffectStates();
    m_dirty = false;
    const auto result = m_storage->save (m_playlists, m_rig, m_banks);

    if (result.failed())
    {
        juce::Logger::writeToLog ("MBoxCoreAudio: save failed: " + result.getErrorMessage());
        auto data = makeObject();
        set (data, "message", "Save failed: " + result.getErrorMessage());
        emit (MBoxComAPI::Event::error, data);
    }

    return result;
}

// Copies the state of every plugin changed over TCP into the model, so it's saved
void MBoxCoreAudio::Impl::captureTouchedEffectStates()
{
    for (const auto& touched : m_touchedEffects)
    {
        auto* effects = getModelEffects (touched.m_target, touched.m_playlist, touched.m_song);
        const auto chains = getRuntimeChains (touched.m_target, touched.m_song);

        if (effects == nullptr || chains.empty() || ! juce::isPositiveAndBelow (touched.m_effectIndex, (int) effects->size()))
            continue;

        if (auto* plugin = chains.front()->getPlugin (touched.m_effectIndex))
        {
            juce::MemoryBlock state;
            plugin->getStateInformation (state);
            (*effects)[(size_t) touched.m_effectIndex].m_state = state;
        }
    }

    m_touchedEffects.clear();
}

//==============================================================================
// Lookups
//==============================================================================
MBoxPlaylist* MBoxCoreAudio::Impl::findPlaylist (const juce::String& name) const
{
    for (const auto& playlist : m_playlists)
        if (playlist->getName().equalsIgnoreCase (name))
            return playlist.get();

    return nullptr;
}

MBoxSong* MBoxCoreAudio::Impl::getSelectedSong() const
{
    if (auto* playlist = findPlaylist (m_loadedPlaylist))
        return playlist->getSongInstance (m_selectedSong);

    return nullptr;
}

MBoxSongPlayer* MBoxCoreAudio::Impl::getRuntimeSong (const juce::String& songName) const
{
    auto* player = getRenderer().getPublishedPlayer();

    if (player == nullptr || m_loadedPlaylist.isEmpty())
        return nullptr;

    for (auto& song : player->m_songs)
        if (song->m_name.equalsIgnoreCase (songName))
            return song.get();

    return nullptr;
}

//==============================================================================
// Loading. The playlist is copied on the message thread, and the copy is turned into a
// player on the loader thread. A newer load (or the destructor) bumps m_loadGeneration,
// which cancels an older one and makes its result be discarded.
//==============================================================================
void MBoxCoreAudio::Impl::startLoad (const juce::String& playlistName, const juce::String& songName, bool reselect)
{
    auto* playlist = findPlaylist (playlistName);
    jassert (playlist != nullptr);

    auto snapshot = std::make_shared<MBoxPlaylist> (*playlist);
    auto banks = std::make_shared<MBoxBanks> (m_banks);
    auto rig = std::make_shared<MBoxRig> (m_rig);
    const auto generation = ++m_loadGeneration;

    m_loading = true;
    m_loadingPlaylist = playlist->getName();
    m_loadingSong = songName;
    m_reselectAfterLoad = reselect;
    m_loadProgress = 0.0f;
    m_reportedProgress = -1.0f;

    juce::WeakReference<Impl> weakThis (this);

    m_loader.addJob ([this, weakThis, snapshot, banks, rig, generation]
    {
        const MBoxPlayerBuildContext context { m_clipCache, m_pluginHost.get(), *banks, *rig,
                                               supportedSampleRate, MBoxRenderer::maxChunkSize, 2.0,
                                               [this, generation] { return generation != m_loadGeneration.load(); },
                                               [this] (float progress) { m_loadProgress = progress; } };
        juce::String error;
        auto player = buildPlaylistPlayer (*snapshot, context, error);

        // std::function needs a copyable callable, so the player travels in a shared holder
        auto holder = std::make_shared<std::unique_ptr<MBoxPlaylistPlayer>> (std::move (player));

        juce::MessageManager::callAsync ([weakThis, holder, generation, error]
        {
            if (auto* impl = weakThis.get())
                impl->finishLoad (generation, std::move (*holder), error);
        });
    });
}

void MBoxCoreAudio::Impl::finishLoad (int generation, std::unique_ptr<MBoxPlaylistPlayer> player, const juce::String& error)
{
    if (generation != m_loadGeneration.load())
        return;  // superseded by a newer load

    m_loading = false;
    auto data = makeObject();
    set (data, "playlist", m_loadingPlaylist);
    set (data, "ok", player != nullptr);
    set (data, "error", error);

    if (player == nullptr)
    {
        juce::Logger::writeToLog ("MBoxCoreAudio: loading " + m_loadingPlaylist + " failed: " + error);
        emit (MBoxComAPI::Event::playlistLoaded, data);
        return;
    }

    auto* playlist = findPlaylist (m_loadingPlaylist);

    if (playlist == nullptr)
        return;  // removed while loading

    // The new playlist's WAVs are all referenced by the player now; drop the others
    juce::Array<juce::File> files;

    for (const auto& song : player->m_songs)
        for (const auto& track : song->m_tracks)
            if (track.m_clip != nullptr)
                files.add (track.m_clip->m_file);

    auto& renderer = getRenderer();
    const auto status = renderer.getStatus();
    renderer.publish (std::move (player));
    m_clipCache.retainOnly (files);
    m_loadedPlaylist = playlist->getName();

    const auto songIndex = juce::jmax (0, playlist->getSongIndex (m_loadingSong));

    if (m_reselectAfterLoad || songIndex != status.m_songIndex)
        renderer.selectSong (songIndex);

    m_selectedSong = juce::isPositiveAndBelow (songIndex, (int) playlist->getSongs().size())
                         ? playlist->getSongs()[(size_t) songIndex]->getName() : juce::String();

    emit (MBoxComAPI::Event::playlistLoaded, data);

    auto songData = makeObject();
    set (songData, "playlist", m_loadedPlaylist);
    set (songData, "song", m_selectedSong);
    emit (MBoxComAPI::Event::songChanged, songData);
}

// After an edit of the loaded playlist: build it again and swap it in, keeping the transport
void MBoxCoreAudio::Impl::rebuildIfLoaded (const juce::String& playlistName)
{
    if (m_loadedPlaylist.isNotEmpty() && m_loadedPlaylist.equalsIgnoreCase (playlistName))
        startLoad (m_loadedPlaylist, m_selectedSong, false);
}

// Only reached by removing the loaded playlist (or its last song), a setup operation: the
// empty player is taken before the stop fade runs, so audio playing at that moment is cut.
void MBoxCoreAudio::Impl::unloadPlaylist()
{
    ++m_loadGeneration;
    m_loading = false;
    getRenderer().stop();
    getRenderer().publish (nullptr);
    m_loadedPlaylist = {};
    m_selectedSong = {};
}

void MBoxCoreAudio::Impl::pushBusToRenderer (int bus)
{
    const auto& settings = m_rig.m_mixBuses[(size_t) bus - 1];
    getRenderer().setBus (bus, settings.getOutputs(), settings.getMasterVolume(), settings.getMasterBalance(),
                          settings.getMetronomeVolume());
}

//==============================================================================
// Effects: the model's list (what's saved) and the running chains (what's heard)
//==============================================================================
std::vector<MBoxEffect>* MBoxCoreAudio::Impl::getModelEffects (const MBoxEffectTarget& target, const juce::String& playlist,
                                                              const juce::String& song)
{
    switch (target.m_kind)
    {
        case MBoxEffectTarget::Kind::Slot:
        {
            const MBoxSlotRef slot { target.m_bank, target.m_slot };
            return slot.isValid() ? &getBankSlot (m_banks, slot).m_effectChain : nullptr;
        }

        case MBoxEffectTarget::Kind::Insert:
        {
            auto* playlistModel = findPlaylist (playlist);
            auto* songModel = playlistModel != nullptr ? playlistModel->getSongInstance (song) : nullptr;
            auto* trackMix = songModel != nullptr ? songModel->getTrackMix (target.m_bus, target.m_track) : nullptr;
            return trackMix != nullptr ? &trackMix->m_inserts : nullptr;
        }

        case MBoxEffectTarget::Kind::Master:
            return juce::isPositiveAndNotGreaterThan (target.m_bus, numberOfMixBus) && target.m_bus >= 1
                       ? &m_rig.m_mixBuses[(size_t) target.m_bus - 1].getMasterEffectChain() : nullptr;
    }

    return nullptr;
}

std::vector<MBoxEffectChain*> MBoxCoreAudio::Impl::getRuntimeChains (const MBoxEffectTarget& target, const juce::String& song) const
{
    std::vector<MBoxEffectChain*> chains;
    auto* player = getRenderer().getPublishedPlayer();

    if (player == nullptr)
        return chains;

    switch (target.m_kind)
    {
        case MBoxEffectTarget::Kind::Slot:
            // A slot used on several inputs runs one chain per input; all are kept in sync
            for (auto& liveInput : player->m_liveInputs)
                if (auto* chain = liveInput->getSlotChain ({ target.m_bank, target.m_slot }))
                    chains.push_back (chain);
            break;

        case MBoxEffectTarget::Kind::Insert:
            if (auto* songPlayer = getRuntimeSong (song))
                if (auto* track = songPlayer->findTrack (target.m_track))
                    if (target.m_bus >= 1 && target.m_bus <= numberOfMixBus)
                        chains.push_back (track->m_inserts[(size_t) target.m_bus - 1].get());
            break;

        case MBoxEffectTarget::Kind::Master:
            if (target.m_bus >= 1 && target.m_bus <= numberOfMixBus)
                chains.push_back (player->m_masterChains[(size_t) target.m_bus - 1].get());
            break;
    }

    return chains;
}

//==============================================================================
// Mix changes: the model (saved) and the running player (heard), then an event
//==============================================================================
MBoxResult MBoxCoreAudio::Impl::changeTrackMix (int bus, int trackId, const std::function<void (MBoxTrackMix&)>& change)
{
    auto* song = getSelectedSong();

    if (song == nullptr)
        return MBoxResult::fail ("No song selected");

    auto* trackMix = song->getTrackMix (bus, trackId);

    if (trackMix == nullptr)
        return MBoxResult::fail ("No track " + std::to_string (trackId) + " in mix bus " + std::to_string (bus));

    change (*trackMix);
    markDirty();

    if (auto* runtimeSong = getRuntimeSong (m_selectedSong))
        if (auto* track = runtimeSong->findTrack (trackId))
            track->m_mix[(size_t) bus - 1]->setFrom (*trackMix);

    auto data = makeObject();
    set (data, "bus", bus);
    set (data, "track", trackId);
    set (data, "volume", trackMix->m_volume);
    set (data, "balance", trackMix->m_balance);
    set (data, "mute", trackMix->m_mute);
    set (data, "solo", trackMix->m_solo);
    emit (MBoxComAPI::Event::mixChanged, data);
    return MBoxResult::ok();
}

MBoxResult MBoxCoreAudio::Impl::changeBus (int bus, const std::function<void (MBoxMixBus&)>& change)
{
    if (bus < 1 || bus > numberOfMixBus)
        return MBoxResult::fail ("No mix bus " + std::to_string (bus));

    auto& settings = m_rig.m_mixBuses[(size_t) bus - 1];
    change (settings);
    pushBusToRenderer (bus);
    markDirty();

    auto data = makeObject();
    set (data, "bus", bus);
    set (data, "masterVolume", settings.getMasterVolume());
    set (data, "masterBalance", settings.getMasterBalance());
    set (data, "metronomeVolume", settings.getMetronomeVolume());
    emit (MBoxComAPI::Event::mixChanged, data);
    return MBoxResult::ok();
}

//==============================================================================
//==============================================================================
// MBoxCoreAudio constructor: initialize JUCE, then the engine, storage and server
//==============================================================================
MBoxCoreAudio::MBoxCoreAudio (const MBoxCoreOptions& options)
    : m_impl (std::make_unique<Impl> (*this, options))
{
}

//==============================================================================
// MBoxCoreAudio destructor: defined here, where Impl is a complete type
//==============================================================================
MBoxCoreAudio::~MBoxCoreAudio() = default;

MBoxResult MBoxCoreAudio::getInitialisationResult() const
{
    return m_impl->m_initialisationErrors.isEmpty()
               ? MBoxResult::ok()
               : MBoxResult::fail (m_impl->m_initialisationErrors.joinIntoString ("; ").toStdString());
}

void MBoxCoreAudio::run()
{
   #if JUCE_MAC
    juce::initialiseNSApplication();  // returns the existing one inside a JUCE app
   #endif

    juce::MessageManager::getInstance()->runDispatchLoop();
}

void MBoxCoreAudio::quit()
{
    juce::MessageManager::getInstance()->stopDispatchLoop();
}

//==============================================================================
// Playlists
//==============================================================================
MBoxResult MBoxCoreAudio::addPlaylist (const std::string& name)
{
    const juce::String playlistName (name);

    if (const auto result = validateItemName (playlistName); result.failed())
        return toResult (result);

    if (m_impl->findPlaylist (playlistName) != nullptr)
        return MBoxResult::fail ("A playlist named " + name + " already exists");

    m_impl->m_playlists.push_back (std::make_unique<MBoxPlaylist> (playlistName));
    m_impl->markDirty();
    return MBoxResult::ok();
}

MBoxResult MBoxCoreAudio::removePlaylist (const std::string& name)
{
    auto& playlists = m_impl->m_playlists;
    auto it = std::find_if (playlists.begin(), playlists.end(),
                            [&] (const auto& playlist) { return playlist->getName().equalsIgnoreCase (juce::String (name)); });

    if (it == playlists.end())
        return MBoxResult::fail ("No playlist " + name);

    if (m_impl->m_loadedPlaylist.equalsIgnoreCase ((*it)->getName()))
        m_impl->unloadPlaylist();

    if (const auto result = m_impl->m_storage->trashPlaylistDirectory ((*it)->getName()); result.failed())
        return toResult (result);

    playlists.erase (it);
    m_impl->markDirty();
    return MBoxResult::ok();
}

std::vector<std::string> MBoxCoreAudio::getPlaylistNames() const
{
    std::vector<std::string> names;

    for (const auto& playlist : m_impl->m_playlists)
        names.push_back (playlist->getName().toStdString());

    return names;
}

MBoxResult MBoxCoreAudio::movePlaylistPosition (const std::string& name, int to)
{
    auto& playlists = m_impl->m_playlists;
    auto it = std::find_if (playlists.begin(), playlists.end(),
                            [&] (const auto& playlist) { return playlist->getName().equalsIgnoreCase (juce::String (name)); });

    if (it == playlists.end())
        return MBoxResult::fail ("No playlist " + name);

    if (to < 0 || to >= (int) playlists.size())
        return MBoxResult::fail ("Position " + std::to_string (to) + " is outside the playlist list");

    auto playlist = std::move (*it);
    playlists.erase (it);
    playlists.insert (playlists.begin() + to, std::move (playlist));
    m_impl->markDirty();
    return MBoxResult::ok();
}

MBoxResult MBoxCoreAudio::renamePlaylist (const std::string& playlistName, const std::string& newName)
{
    auto* playlist = m_impl->findPlaylist (playlistName);

    if (playlist == nullptr)
        return MBoxResult::fail ("No playlist " + playlistName);

    if (const auto result = validateItemName (newName); result.failed())
        return toResult (result);

    if (auto* existing = m_impl->findPlaylist (newName); existing != nullptr && existing != playlist)
        return MBoxResult::fail ("A playlist named " + newName + " already exists");

    const auto oldName = playlist->getName();
    const auto oldDirectory = m_impl->m_storage->getPlaylistDirectory (oldName);

    if (const auto result = m_impl->m_storage->renamePlaylistDirectory (oldName, newName); result.failed())
        return toResult (result);

    playlist->rename (newName);

    for (const auto& song : playlist->getSongs())
        rebaseAudioFiles (*song, oldDirectory, m_impl->m_storage->getPlaylistDirectory (newName));

    if (m_impl->m_loadedPlaylist == oldName)
    {
        m_impl->m_loadedPlaylist = newName;

        if (auto* player = m_impl->getRenderer().getPublishedPlayer())
            player->m_playlistName = newName;
    }

    m_impl->markDirty();
    return MBoxResult::ok();
}

//==============================================================================
// Songs
//==============================================================================
MBoxResult MBoxCoreAudio::addSong (const std::string& playlistName, const std::string& songName)
{
    auto* playlist = m_impl->findPlaylist (playlistName);

    if (playlist == nullptr)
        return MBoxResult::fail ("No playlist " + playlistName);

    if (const auto result = playlist->addSong (juce::String (songName)); result.failed())
        return toResult (result);

    m_impl->markDirty();
    m_impl->rebuildIfLoaded (playlist->getName());
    return MBoxResult::ok();
}

MBoxResult MBoxCoreAudio::removeSong (const std::string& playlistName, const std::string& songName)
{
    auto* playlist = m_impl->findPlaylist (playlistName);

    if (playlist == nullptr)
        return MBoxResult::fail ("No playlist " + playlistName);

    auto* song = playlist->getSongInstance (songName);

    if (song == nullptr)
        return MBoxResult::fail ("No song " + songName + " in playlist " + playlistName);

    const auto isLoaded = m_impl->m_loadedPlaylist.equalsIgnoreCase (playlist->getName());

    if (isLoaded && m_impl->m_selectedSong.equalsIgnoreCase (song->getName()))
    {
        m_impl->getRenderer().stop();
        m_impl->m_selectedSong = playlist->getSongs().size() > 1 ? playlist->getSongs().front()->getName() : juce::String();
    }

    if (const auto result = m_impl->m_storage->trashSongDirectory (playlist->getName(), song->getName()); result.failed())
        return toResult (result);

    playlist->removeSong (song->getName());
    m_impl->markDirty();

    if (isLoaded && playlist->getSongs().empty())
        m_impl->unloadPlaylist();
    else
        m_impl->rebuildIfLoaded (playlist->getName());

    return MBoxResult::ok();
}

std::vector<std::string> MBoxCoreAudio::getSongNames (const std::string& playlistName) const
{
    if (auto* playlist = m_impl->findPlaylist (playlistName))
        return toStdStrings (playlist->getSongNames());

    return {};
}

MBoxResult MBoxCoreAudio::moveSong (const std::string& playlistName, const std::string& songName, int to)
{
    auto* playlist = m_impl->findPlaylist (playlistName);

    if (playlist == nullptr)
        return MBoxResult::fail ("No playlist " + playlistName);

    if (const auto result = playlist->moveSong (songName, to); result.failed())
        return toResult (result);

    m_impl->markDirty();
    m_impl->rebuildIfLoaded (playlist->getName());
    return MBoxResult::ok();
}

MBoxResult MBoxCoreAudio::renameSong (const std::string& playlistName, const std::string& songName, const std::string& newName)
{
    auto* playlist = m_impl->findPlaylist (playlistName);

    if (playlist == nullptr)
        return MBoxResult::fail ("No playlist " + playlistName);

    auto* song = playlist->getSongInstance (songName);

    if (song == nullptr)
        return MBoxResult::fail ("No song " + songName + " in playlist " + playlistName);

    if (const auto result = validateItemName (newName); result.failed())
        return toResult (result);

    if (auto* existing = playlist->getSongInstance (newName); existing != nullptr && existing != song)
        return MBoxResult::fail ("Playlist " + playlistName + " already has a song named " + newName);

    const auto oldName = song->getName();
    auto& storage = *m_impl->m_storage;

    if (const auto result = storage.renameSongDirectory (playlist->getName(), oldName, newName); result.failed())
        return toResult (result);

    playlist->renameSong (oldName, newName);
    rebaseAudioFiles (*song, storage.getSongDirectory (playlist->getName(), oldName),
                      storage.getSongDirectory (playlist->getName(), newName));

    if (m_impl->m_selectedSong == oldName && m_impl->m_loadedPlaylist.equalsIgnoreCase (playlist->getName()))
        m_impl->m_selectedSong = newName;

    m_impl->markDirty();
    m_impl->rebuildIfLoaded (playlist->getName());
    return MBoxResult::ok();
}

MBoxResult MBoxCoreAudio::selectSong (const std::string& playlistName, const std::string& songName)
{
    auto* playlist = m_impl->findPlaylist (playlistName);

    if (playlist == nullptr)
        return MBoxResult::fail ("No playlist " + playlistName);

    const auto songIndex = playlist->getSongIndex (songName);

    if (songIndex < 0)
        return MBoxResult::fail ("No song " + songName + " in playlist " + playlistName);

    const auto& songs = playlist->getSongs();

    if (m_impl->m_loadedPlaylist == playlist->getName() && ! m_impl->m_loading)
    {
        m_impl->getRenderer().selectSong (songIndex);
        m_impl->m_selectedSong = songs[(size_t) songIndex]->getName();

        auto data = makeObject();
        set (data, "playlist", m_impl->m_loadedPlaylist);
        set (data, "song", m_impl->m_selectedSong);
        m_impl->emit (MBoxComAPI::Event::songChanged, data);
        return MBoxResult::ok();
    }

    m_impl->getRenderer().stop();
    m_impl->startLoad (playlist->getName(), songs[(size_t) songIndex]->getName(), true);
    return MBoxResult::ok();
}

//==============================================================================
// Direct model access, for the UI
//==============================================================================
MBoxSong* MBoxCoreAudio::getSongInstance (const std::string& playlistName, const std::string& songName)
{
    if (auto* playlist = m_impl->findPlaylist (playlistName))
        return playlist->getSongInstance (songName);

    return nullptr;
}

MBoxRig* MBoxCoreAudio::getRig()                    { return &m_impl->m_rig; }
MBoxBank* MBoxCoreAudio::getBanks()                 { return m_impl->m_banks.data(); }
MBoxAudioEngine* MBoxCoreAudio::getAudioEngine()    { return m_impl->m_audioEngine.get(); }

MBoxResult MBoxCoreAudio::songChanged (const std::string& playlistName, const std::string& songName)
{
    if (getSongInstance (playlistName, songName) == nullptr)
        return MBoxResult::fail ("No song " + songName + " in playlist " + playlistName);

    m_impl->markDirty();
    m_impl->rebuildIfLoaded (playlistName);
    return MBoxResult::ok();
}

MBoxResult MBoxCoreAudio::rigChanged()
{
    for (int bus = 1; bus <= numberOfMixBus; ++bus)
        m_impl->pushBusToRenderer (bus);

    m_impl->markDirty();
    m_impl->rebuildIfLoaded (m_impl->m_loadedPlaylist);
    return MBoxResult::ok();
}

MBoxResult MBoxCoreAudio::banksChanged()
{
    m_impl->markDirty();
    m_impl->rebuildIfLoaded (m_impl->m_loadedPlaylist);
    return MBoxResult::ok();
}

MBoxResult MBoxCoreAudio::save()
{
    return toResult (m_impl->doSave());
}

//==============================================================================
// Transport
//==============================================================================
MBoxResult MBoxCoreAudio::play()
{
    if (m_impl->m_loading)
        return MBoxResult::fail ("A playlist is loading");

    if (m_impl->m_loadedPlaylist.isEmpty())
        return MBoxResult::fail ("No playlist loaded: select a song first");

    m_impl->getRenderer().play();
    return MBoxResult::ok();
}

MBoxResult MBoxCoreAudio::pause()
{
    m_impl->getRenderer().pause();
    return MBoxResult::ok();
}

MBoxResult MBoxCoreAudio::stop()
{
    m_impl->getRenderer().stop();
    return MBoxResult::ok();
}

MBoxTransportInfo MBoxCoreAudio::getTransportInfo() const
{
    MBoxTransportInfo info;
    const auto status = m_impl->getRenderer().getStatus();
    info.m_state = toString (status.m_state);
    info.m_loading = m_impl->m_loading;
    info.m_loadingProgress = m_impl->m_loadProgress;

    auto* player = m_impl->getRenderer().getPublishedPlayer();

    if (player == nullptr || ! juce::isPositiveAndBelow (status.m_songIndex, (int) player->m_songs.size()))
        return info;

    const auto& song = *player->m_songs[(size_t) status.m_songIndex];
    const auto& timeline = song.m_timeline;
    const auto position = timeline.framesToBar (status.m_position);
    const auto signature = timeline.getSignatureAtBar (position.m_bar);

    info.m_playlist = player->m_playlistName.toStdString();
    info.m_song = song.m_name.toStdString();
    info.m_position = position.toString();
    info.m_bpm = timeline.getTempoAtTicks (timeline.barToTicks (position));
    info.m_numerator = signature.m_numerator;
    info.m_denominator = signature.m_denominator;
    return info;
}

//==============================================================================
// Mixing
//==============================================================================
MBoxResult MBoxCoreAudio::setTrackVolume (int bus, int trackId, double volumeDb)
{
    return m_impl->changeTrackMix (bus, trackId, [&] (MBoxTrackMix& mix) { mix.m_volume = juce::jmin (maxVolumeDb, volumeDb); });
}

MBoxResult MBoxCoreAudio::setTrackBalance (int bus, int trackId, double balance)
{
    return m_impl->changeTrackMix (bus, trackId, [&] (MBoxTrackMix& mix) { mix.m_balance = juce::jlimit (-1.0, 1.0, balance); });
}

MBoxResult MBoxCoreAudio::setTrackMute (int bus, int trackId, bool mute)
{
    return m_impl->changeTrackMix (bus, trackId, [&] (MBoxTrackMix& mix) { mix.m_mute = mute; });
}

MBoxResult MBoxCoreAudio::setTrackSolo (int bus, int trackId, bool solo)
{
    return m_impl->changeTrackMix (bus, trackId, [&] (MBoxTrackMix& mix) { mix.m_solo = solo; });
}

MBoxResult MBoxCoreAudio::setBusVolume (int bus, double volumeDb)
{
    return m_impl->changeBus (bus, [&] (MBoxMixBus& settings) { settings.setMasterVolume (volumeDb); });
}

MBoxResult MBoxCoreAudio::setBusBalance (int bus, double balance)
{
    return m_impl->changeBus (bus, [&] (MBoxMixBus& settings) { settings.setMasterBalance (balance); });
}

MBoxResult MBoxCoreAudio::setMetronomeVolume (int bus, double volumeDb)
{
    return m_impl->changeBus (bus, [&] (MBoxMixBus& settings) { settings.setMetronomeVolume (volumeDb); });
}

MBoxResult MBoxCoreAudio::setMetronomeState (const std::string& state)
{
    MetronomeState metronome;

    if (! fromString (state, metronome))
        return MBoxResult::fail ("Unknown metronome state " + state + " (expected off, fourCountdownOnly or on)");

    auto* song = m_impl->getSelectedSong();

    if (song == nullptr)
        return MBoxResult::fail ("No song selected");

    song->setMetronomeState (metronome);
    m_impl->markDirty();

    // The clicks are computed when the playlist is built
    m_impl->rebuildIfLoaded (m_impl->m_loadedPlaylist);
    return MBoxResult::ok();
}

std::string MBoxCoreAudio::getMixJson (int bus) const
{
    juce::Array<juce::var> tracks;
    auto* song = m_impl->getSelectedSong();

    if (song != nullptr && bus >= 1 && bus <= numberOfMixBus)
    {
        for (const auto& trackMix : song->getMix (bus))
        {
            auto* track = song->getTrack (trackMix.m_trackId);
            auto entry = makeObject();
            set (entry, "trackId", trackMix.m_trackId);
            set (entry, "name", track != nullptr ? track->getName() : juce::String());
            set (entry, "type", juce::String (track != nullptr ? toString (track->getType()) : ""));
            set (entry, "volume", trackMix.m_volume);
            set (entry, "balance", trackMix.m_balance);
            set (entry, "mute", trackMix.m_mute);
            set (entry, "solo", trackMix.m_solo);
            tracks.add (entry);
        }
    }

    return toCompactJson (tracks);
}

//==============================================================================
// Effects
//==============================================================================
std::vector<MBoxEffectInfo> MBoxCoreAudio::getEffects (const MBoxEffectTarget& target) const
{
    std::vector<MBoxEffectInfo> result;

    if (auto* effects = m_impl->getModelEffects (target, m_impl->m_loadedPlaylist, m_impl->m_selectedSong))
        for (const auto& effect : *effects)
            result.push_back ({ effect.m_name.toStdString(), effect.m_manufacturer.toStdString(), effect.m_bypassed });

    return result;
}

std::vector<MBoxParameterInfo> MBoxCoreAudio::getEffectParameters (const MBoxEffectTarget& target, int effectIndex) const
{
    std::vector<MBoxParameterInfo> result;
    const auto chains = m_impl->getRuntimeChains (target, m_impl->m_selectedSong);

    if (chains.empty())
        return result;

    if (auto* plugin = chains.front()->getPlugin (effectIndex))
        for (auto* parameter : plugin->getParameters())
            if (auto* hosted = dynamic_cast<juce::HostedAudioProcessorParameter*> (parameter))
                result.push_back ({ hosted->getParameterID().toStdString(), parameter->getName (64).toStdString(), parameter->getValue() });

    return result;
}

MBoxResult MBoxCoreAudio::setEffectParameter (const MBoxEffectTarget& target, int effectIndex, const std::string& parameterId, float value)
{
    const auto chains = m_impl->getRuntimeChains (target, m_impl->m_selectedSong);

    if (chains.empty())
        return MBoxResult::fail ("This effect isn't running: load a playlist that uses it first");

    bool found = false;

    for (auto* chain : chains)
        if (auto* plugin = chain->getPlugin (effectIndex))
            for (auto* parameter : plugin->getParameters())
                if (auto* hosted = dynamic_cast<juce::HostedAudioProcessorParameter*> (parameter))
                    if (hosted->getParameterID() == juce::String (parameterId))
                    {
                        parameter->setValueNotifyingHost (juce::jlimit (0.0f, 1.0f, value));
                        found = true;
                    }

    if (! found)
        return MBoxResult::fail ("No parameter " + parameterId + " in effect " + std::to_string (effectIndex));

    m_impl->m_touchedEffects.push_back ({ target, effectIndex, m_impl->m_loadedPlaylist, m_impl->m_selectedSong });
    m_impl->markDirty();
    return MBoxResult::ok();
}

MBoxResult MBoxCoreAudio::setEffectBypass (const MBoxEffectTarget& target, int effectIndex, bool bypassed)
{
    auto* effects = m_impl->getModelEffects (target, m_impl->m_loadedPlaylist, m_impl->m_selectedSong);

    if (effects == nullptr || ! juce::isPositiveAndBelow (effectIndex, (int) effects->size()))
        return MBoxResult::fail ("No effect " + std::to_string (effectIndex) + " there");

    (*effects)[(size_t) effectIndex].m_bypassed = bypassed;

    for (auto* chain : m_impl->getRuntimeChains (target, m_impl->m_selectedSong))
        chain->setBypassed (effectIndex, bypassed);

    m_impl->markDirty();
    return MBoxResult::ok();
}

MBoxResult MBoxCoreAudio::scanForPlugins()
{
    return toResult (m_impl->m_pluginHost->scanForPlugins());
}

//==============================================================================
// Audio devices
//==============================================================================
std::vector<std::string> MBoxCoreAudio::getAvailableDeviceTypes() const
{
    return toStdStrings (m_impl->m_audioEngine->getAvailableDeviceTypes());
}

std::vector<std::string> MBoxCoreAudio::getAvailableDeviceNames (const std::string& type, bool forInput) const
{
    return toStdStrings (m_impl->m_audioEngine->getAvailableDeviceNames (type, forInput));
}

MBoxDeviceInfo MBoxCoreAudio::getCurrentDevice() const
{
    const auto settings = m_impl->m_audioEngine->getCurrentDeviceSettings();
    MBoxDeviceInfo info;
    info.m_type = settings.m_typeName.toStdString();
    info.m_inputDevice = settings.m_inputDeviceName.toStdString();
    info.m_outputDevice = settings.m_outputDeviceName.toStdString();
    info.m_bufferSize = settings.m_bufferSize;
    info.m_sampleRate = m_impl->m_audioEngine->getSampleRate();
    return info;
}

std::vector<int> MBoxCoreAudio::getAvailableBufferSizes() const
{
    const auto sizes = m_impl->m_audioEngine->getAvailableBufferSizes();
    return { sizes.begin(), sizes.end() };
}

MBoxResult MBoxCoreAudio::setAudioDevice (const std::string& type, const std::string& inputDevice, const std::string& outputDevice)
{
    auto& engine = *m_impl->m_audioEngine;
    const auto previous = engine.getCurrentDeviceSettings();

    MBoxDeviceSettings settings;
    settings.m_typeName = type;
    settings.m_inputDeviceName = inputDevice;
    settings.m_outputDeviceName = outputDevice;
    settings.m_bufferSize = m_impl->m_rig.m_device.m_bufferSize;

    const auto result = engine.setDevice (settings);

    if (result.failed())
    {
        // Back to the previous device, or at least keep the transport running
        if (previous.isSystemDefault() || engine.setDevice (previous).failed())
            engine.startWithoutDevice();

        return toResult (result);
    }

    m_impl->m_rig.m_device = engine.getCurrentDeviceSettings();
    m_impl->markDirty();

    const auto info = getCurrentDevice();
    auto data = makeObject();
    set (data, "type", juce::String (info.m_type));
    set (data, "input", juce::String (info.m_inputDevice));
    set (data, "output", juce::String (info.m_outputDevice));
    set (data, "bufferSize", info.m_bufferSize);
    set (data, "sampleRate", info.m_sampleRate);
    m_impl->emit (MBoxComAPI::Event::deviceChanged, data);
    return MBoxResult::ok();
}

MBoxResult MBoxCoreAudio::setBufferSize (int bufferSize)
{
    const auto result = m_impl->m_audioEngine->setBufferSize (bufferSize);

    if (result.failed())
        return toResult (result);

    m_impl->m_rig.m_device = m_impl->m_audioEngine->getCurrentDeviceSettings();
    m_impl->markDirty();
    return MBoxResult::ok();
}

//==============================================================================
// Events
//==============================================================================
int MBoxCoreAudio::addEventListener (std::function<void (const MBoxEvent&)> listener)
{
    const auto id = m_impl->m_nextListenerId++;
    m_impl->m_listeners[id] = std::move (listener);
    return id;
}

void MBoxCoreAudio::removeEventListener (int listenerId)
{
    m_impl->m_listeners.erase (listenerId);
}
