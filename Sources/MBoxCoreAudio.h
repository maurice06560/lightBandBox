#pragma once

#include "mbox_export.h"

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

// Forward declarations keep JUCE out of the library's public header. These classes are only
// usable by apps that compile the library in (MBoxCoreEmbedded, the UI app).
class MBoxAudioEngine;
class MBoxSong;
struct MBoxRig;
struct MBoxBank;

//==============================================================================
// The outcome of an API call. message explains a failure.
//==============================================================================
struct MBOX_EXPORT MBoxResult
{
    bool m_ok { true };
    std::string m_message;

    static MBoxResult ok()                         { return {}; }
    static MBoxResult fail (std::string message)  { return { false, std::move (message) }; }
    bool wasOk() const noexcept                    { return m_ok; }
};

//==============================================================================
// Something that happened, for the UI and the TCP clients. m_name is the event name of the
// protocol (transport, playlistLoading, playlistLoaded, songChanged, mixChanged,
// deviceChanged, error) and m_json its data as a JSON object.
//==============================================================================
struct MBOX_EXPORT MBoxEvent
{
    std::string m_name;
    std::string m_json;
};

struct MBOX_EXPORT MBoxTransportInfo
{
    std::string m_state { "stopped" };  // "stopped", "playing" or "paused"
    std::string m_playlist;              // the loaded playlist, empty when none
    std::string m_song;
    std::string m_position { "1.1.1.0" };
    int m_bpm { 120 };
    int m_numerator { 4 };
    int m_denominator { 4 };
    bool m_loading { false };
    float m_loadingProgress { 0.0f };    // 0 to 1 while loading
};

struct MBOX_EXPORT MBoxDeviceInfo
{
    std::string m_type;
    std::string m_inputDevice;
    std::string m_outputDevice;
    int m_bufferSize { 0 };
    double m_sampleRate { 0.0 };
    int m_numInputs { 0 };
    int m_numOutputs { 0 };
};

// Which effect chain an effect call refers to. Numbers are 1-based.
struct MBOX_EXPORT MBoxEffectTarget
{
    enum class Kind { Slot, Insert, Master };

    Kind m_kind { Kind::Slot };
    int m_bank { 1 };   // Slot
    int m_slot { 1 };   // Slot
    int m_bus { 1 };    // Insert, Master
    int m_track { 1 };  // Insert: a track id of the selected song
};

struct MBOX_EXPORT MBoxEffectInfo
{
    std::string m_name;
    std::string m_manufacturer;
    bool m_bypassed { false };
};

struct MBOX_EXPORT MBoxParameterInfo
{
    std::string m_id;
    std::string m_name;
    float m_value { 0.0f };  // normalised, 0 to 1
};

struct MBOX_EXPORT MBoxCoreOptions
{
    std::string m_dataDirectory;      // empty: ONE_BOX_DATA_DIR, or <user home>/OneBox
    int m_tcpPort { -1 };             // -1: ONE_BOX_DATA_TCP_PORT, or 5050. 0: no TCP server
    bool m_openAudioDevice { true };  // false runs without audio (tests)
};

//==============================================================================
// This class is the entry point for the dynamic library.
// All applications create a unique instance of it and interact with the audio via the
// public APIs. A second instance, in this process or another one, fails to initialise.
//
// Threads: every method must be called on the thread that created the object, which
// becomes JUCE's message thread. The headless app calls run() on it; a JUCE app's own
// message loop already serves it. Methods never block on audio: transport and mix changes
// are queued to the audio thread, and loading a playlist happens in the background.
//
// Names: playlist and song names are folder names, unique (ignoring case) and checked by
// validateItemName(). Positions in the move methods are 0-based. Bus numbers are 1-based.
//==============================================================================
class MBOX_EXPORT MBoxCoreAudio {

    public:
    explicit MBoxCoreAudio (const MBoxCoreOptions& options = {});
    ~MBoxCoreAudio();

    MBoxCoreAudio (const MBoxCoreAudio&) = delete;
    MBoxCoreAudio& operator= (const MBoxCoreAudio&) = delete;

    // Whether startup worked: the single-instance lock, loading the data folder, the audio
    // device and the TCP server. A failed device or server still leaves the rest usable.
    MBoxResult getInitialisationResult() const;

    // Runs JUCE's message loop until quit() is called. For apps without their own loop.
    void run();
    void quit();

    //==========================================================================
    // Playlists and songs (saved automatically about 1 s after the last change)
    MBoxResult addPlaylist (const std::string& name);
    MBoxResult removePlaylist (const std::string& name);
    std::vector<std::string> getPlaylistNames() const;
    MBoxResult movePlaylistPosition (const std::string& name, int to);
    MBoxResult renamePlaylist (const std::string& playlistName, const std::string& newName);

    MBoxResult addSong (const std::string& playlistName, const std::string& songName);
    MBoxResult removeSong (const std::string& playlistName, const std::string& songName);
    std::vector<std::string> getSongNames (const std::string& playlistName) const;
    MBoxResult moveSong (const std::string& playlistName, const std::string& songName, int to);
    MBoxResult renameSong (const std::string& playlistName, const std::string& songName, const std::string& newName);

    // Selects a song. Selecting a song of another playlist stops playback and loads that
    // playlist in the background (decoding every WAV, creating and warming up every plugin);
    // playlistLoading and playlistLoaded events report the progress. Within the loaded
    // playlist, it stops (with a fade) and moves to the song's left locator.
    MBoxResult selectSong (const std::string& playlistName, const std::string& songName);

    // Direct access to the model, for the UI. After changing a song, the rig or the banks,
    // call the matching ...Changed() method so it's saved and the loaded playlist rebuilt.
    MBoxSong* getSongInstance (const std::string& playlistName, const std::string& songName);
    MBoxRig* getRig();
    MBoxBank* getBanks();  // an array of numberOfBanks banks
    MBoxResult songChanged (const std::string& playlistName, const std::string& songName);
    MBoxResult rigChanged();
    MBoxResult banksChanged();
    MBoxAudioEngine* getAudioEngine();

    // Writes the data folder now
    MBoxResult save();

    //==========================================================================
    // Transport
    MBoxResult play();
    MBoxResult pause();
    MBoxResult stop();
    MBoxTransportInfo getTransportInfo() const;

    //==========================================================================
    // Mixing, for the selected song. Volumes in dB, balances from -1 to +1.
    MBoxResult setTrackVolume (int bus, int trackId, double volumeDb);
    MBoxResult setTrackBalance (int bus, int trackId, double balance);
    MBoxResult setTrackMute (int bus, int trackId, bool mute);
    MBoxResult setTrackSolo (int bus, int trackId, bool solo);
    MBoxResult setBusVolume (int bus, double volumeDb);
    MBoxResult setBusBalance (int bus, double balance);
    MBoxResult setMetronomeVolume (int bus, double volumeDb);
    // "off", "fourCountdownOnly" or "on", for the selected song
    MBoxResult setMetronomeState (const std::string& state);
    // The selected song's mix of one bus, as a JSON array of
    // {trackId, name, type, volume, balance, mute, solo}
    std::string getMixJson (int bus) const;

    //==========================================================================
    // Effects. Parameter calls need the playlist to be loaded, since they act on the running
    // plugins; the new values are saved in the data folder.
    std::vector<MBoxEffectInfo> getEffects (const MBoxEffectTarget& target) const;
    std::vector<MBoxParameterInfo> getEffectParameters (const MBoxEffectTarget& target, int effectIndex) const;
    MBoxResult setEffectParameter (const MBoxEffectTarget& target, int effectIndex, const std::string& parameterId, float value);
    MBoxResult setEffectBypass (const MBoxEffectTarget& target, int effectIndex, bool bypassed);

    // Scans the installed plugins and saves the list (slow, UI only)
    MBoxResult scanForPlugins();

    //==========================================================================
    // Audio devices. A selected device and buffer size are saved and reopened at startup.
    std::vector<std::string> getAvailableDeviceTypes() const;
    std::vector<std::string> getAvailableDeviceNames (const std::string& type, bool forInput) const;
    MBoxDeviceInfo getCurrentDevice() const;
    std::vector<int> getAvailableBufferSizes() const;
    // Empty names mean the type's default devices
    MBoxResult setAudioDevice (const std::string& type, const std::string& inputDevice, const std::string& outputDevice);
    MBoxResult setBufferSize (int bufferSize);

    //==========================================================================
    // Events, called on the message thread. Returns an id for removeEventListener.
    int addEventListener (std::function<void (const MBoxEvent&)> listener);
    void removeEventListener (int listenerId);

    private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};
