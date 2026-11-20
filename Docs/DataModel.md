# MBox data model

Copied from the Lucidchart "One Box Band" diagram (PDF export, 2026-11-20). Names and types are
reproduced as drawn. Lines marked **[diagram]** note an apparent typo or inconsistency in the
original, kept here unresolved until it is decided.

Global constants (from the design document):

```cpp
constexpr int numberOfMixBus          = 3;
constexpr int numberOfBanks           = 5;
constexpr int numberOfSlotsPerBanks   = 5;
constexpr int maxNumberOfTrackPerSong = 16;
```

## Ownership overview

```
MBoxCoreAudio
├── owns  MBoxPlaylist (n)
│         └── owns  MBoxSong (n)
│                   └── owns  MBoxTrack (n, max 16)
│                             ├── MBoxAudioTrack     (extends MBoxTrack)
│                             ├── MBoxPhysicalTrack  (extends MBoxTrack)
│                             └── MBoxInfoTrack      (extends MBoxTrack)
├── owns  MBoxAudioEngine
│         └── owns  MBoxMixBus (n)
│                   ├── owns  MBoxTrackMix (n) ──non-owning──> MBoxTrack
│                   └── has   MBoxMetronome
├── owns  MBoxServer
└── owns  MBoxStorage

MBoxBank ── has 5 ──> MBoxBankSlot ── chain of ──> IAudioEffectInstance ──> IAudioEffect
MBoxPhysicalTrack ── automation: (Bar, BankSlot*) ──> MBoxBankSlot
MBoxClipStreamer extends juce::TimeSliceClient
```

## Value types

```cpp
struct Bar
{
    int m_bar      { 1 };
    int m_beat     { 1 };
    int m_division { 1 };
    int m_tick     { 0 };
};

struct Signature
{
    int m_numerator   { 4 };
    int m_denominator { 4 };
};

enum EndSongAction   { Stop, Continue };
enum MetronomeState  { Off, FourCountdownOnly, On };
```

## MBoxCoreAudio

```cpp
juce::Result      addPlaylist (const juce::String& name);
juce::Result      removePlaylist (const juce::String& name);
juce::StringArray getPlaylistNames() const;
juce::Result      movePlaylistPosition (const juce::String& name, int to);
juce::Result      addSong (const juce::String& playlistName, const juce::String& songName);
juce::Result      removeSong (const juce::String& playlistName, const juce::String& songName);
juce::StringArray getSongNames (const juce::String& playlistName);
juce::Result      moveSong (const juce::String& playlistName, const juce::String& songName, int to);
juce::Result      renamePlaylist (const juce::String& playlistName, const juce::String& newName);
juce::Result      renameSong (const juce::String& playlistName, const juce::String& songName, const juce::String& newName);
juce::Result      selectSong (const juce::String& playlistName, const juce::String& songName);
MBoxSong*         getSongInstance (const juce::String& playlistName, const juce::String& songName);
MBoxAudioEngine*  getAudioEngine();
juce::Result      play();
juce::Result      pause();
juce::Result      stop();

std::vector<std::unique_ptr<MBoxPlaylist>> m_playlists;   // [diagram] closing '>' missing
std::unique_ptr<MBoxAudioEngine>           m_audioEngine;
std::unique_ptr<MBoxServer>                m_server;
std::unique_ptr<MBoxStorage>               m_storage;
```

## MBoxPlaylist

```cpp
MBoxPlaylist (const juce::String& name);
juce::Result rename (const juce::String& name, const juce::String& newName);
juce::Result addSong (const juce::String& songName);
juce::Result removeSong (const juce::String& songName);
MBoxSong*    getSongInstance (const juce::String& songName);

juce::String                           m_name;
std::vector<std::unique_ptr<MBoxSong>> m_songs;
```

## MBoxSong

```cpp
MBoxSong (const std::string& name);                       // [diagram] std::string, elsewhere juce::String
juce::Result rename (const juce::String& name, const juce::String& newName);
void setTotalBar (const int bar);
int  getTotalBar() const;
void addTempo (const std::pair<Bar, int>& tempo);
void removeTempo (const Bar& tempo);
void addSignature (const std::pair<Bar, Signature>& signature);
void removeSignature (const Bar& signature);

std::string                             m_name;
std::vector<std::pair<Bar, int>>        m_tempoGrid;      // [diagram] closing '>' missing
std::vector<std::pair<Bar, Signature>>  m_signatureGrid;  // [diagram] closing '>' missing
int                                     m_totalNbOfBars { DEFAULT_TOTAL_BAR };
Bar                                     m_leftLocator  { 1, 1, 1, 0 };
Bar                                     m_rightLocator { DEFAULT_TOTAL_BAR, 1, 1, 0 };
std::vector<std::unique_ptr<MBoxTrack>> m_tracks;
EndSongAction                           m_endSongAction { EndSongAction::Stop };
int                                     m_endStopDelay { 0 };  // [diagram] the design doc calls it m_endSongDelay
```

## MBoxTrack and subclasses

```cpp
class MBoxTrack
{
    void         setName (const juce::String& name);
    juce::String getName() const;

    int          m_id { 1 };
    juce::String m_name;
};

class MBoxAudioTrack : public MBoxTrack
{
    void       setAudioStart (const Bar& bar);
    Bar        getAudioStart() const;                     // [diagram] drawn as "Bar void getAudioStart() const"
    void       setFilename (const juce::File& filename);
    juce::File getFilename() const;

    Bar              m_audioStart { 1, 1, 1, 0 };
    juce::File       m_filename;
    MBoxClipStreamer m_clipStreamer;
};

class MBoxPhysicalTrack : public MBoxTrack
{
    void setPhysicalInput (const int physicalInput);
    int  getPhysicalInput() const;
    void setDIOutput (const int DIOutput);
    int  getDIOutput() const;

    int m_physicalInput { 0 };
    int m_DIOutput { -1 };
    std::vector<std::pair<Bar, BankSlot*>> m_bankSlots;   // [diagram] BankSlot, elsewhere MBoxBankSlot
};

class MBoxInfoTrack : public MBoxTrack
{
    std::vector<Bar> m_infoClicks;
};
```

## MBoxAudioEngine

```cpp
juce::Result initialise();

std::vector<std::unique_ptr<MBoxMixBus>> m_mixBuses;      // [diagram] spelled MBoxMixBux, std:: missing on unique_ptr
juce::TimeSliceThread                    m_readAheadThread { "MBox read-ahead" };
```

## MBoxMixBus

```cpp
MBoxMixBus (const int id);
bool                rename (const juce::String& name);
juce::String        getName() const;
bool                setOutputs (const std::pair<int, int>& outputs);
std::pair<int, int> getOutputs();

int                                        m_busId { 1 };
juce::String                               m_name { "MixBus $m_busId" };   // pseudo-code: "MixBus " + id
std::vector<std::unique_ptr<MBoxTrackMix>> m_trackMix;
std::vector<IAudioEffectInstance>          m_masterEffectChain;
double                                     m_masterVolume { 0.0 };
double                                     m_masterBalance { 0.0 };
std::pair<int, int>                        m_outputs;
MBoxMetronome                              m_metronome;
```

## MBoxTrackMix

```cpp
double                            m_volume { 0.0 };
double                            m_balance { 0.0 };
MBoxTrack*                        m_track;                // non-owning
std::vector<IAudioEffectInstance> m_inserts;
```

## MBoxMetronome

```cpp
double         m_volume { 0.0 };
MetronomeState m_state { Off };
```

## Banks and effects

```cpp
class MBoxBank
{
    std::string                                m_name;
    std::array<MBoxBankSlot, NB_OF_BANKS>      m_bankSlots;  // [diagram] "std::arrray"; size should be numberOfSlotsPerBanks
};

class MBoxBankSlot
{
    std::string                     m_name;
    std::list<IAudioEffectInstance> m_effectChain;
};

class IAudioEffectInstance
{
    IAudioEffect* m_audioEffect { nullptr };
};
```

## Changes decided since the diagram

- MBoxCore is built as a static library for the JUCE UI application (one copy of JUCE in the
  process) and as a shared library for the headless rack application.

### Model data and runtime objects are separate

- Model classes are plain data, edited on the message thread and saved by `MBoxStorage`.
  `MBoxAudioTrack` no longer owns an `MBoxClipStreamer`.
- When a playlist is loaded, the engine builds runtime objects from the model and links them by
  track `m_id`: an `MBoxPlaylistPlayer` holding one `MBoxSongPlayer` per song, each holding one
  `MBoxClipStreamer` per audio track. The audio thread reads them through one atomic pointer.
  An edit rebuilds the affected `MBoxSongPlayer` off the audio thread and swaps it between blocks.
- An `MBoxAudioClipCache` holds each decoded WAV once (16-bit stereo, kept as 16-bit in RAM).
- `MBoxClipStreamer` keeps no read position: it renders from `songFrame - startFrame`, so the
  transport's `int64` frame counter is the only position. Everything is preloaded when the
  playlist is selected, so there is no disk streaming and no `m_readAheadThread`.

### Mixes

- Global, once for the rack: `MBoxMixBus` keeps `m_busId`, `m_name`, `m_outputs`,
  `m_masterEffectChain`, `m_masterVolume`, `m_masterBalance`, and the metronome volume.
- Per song, saved in `song.json`: `MBoxSong` owns
  `std::array<std::vector<MBoxTrackMix>, numberOfMixBus> m_mixes;` and the metronome state.
- The metronome state is one value per song, shared by all mixes.
- `MBoxTrackMix` refers to its track by `m_trackId`, not `MBoxTrack*`, and gains
  `bool m_mute { false };` and `bool m_solo { false };` (saved). Solos add up: every soloed track
  of the bus plays.
- Volumes are in dB (0.0 is unity gain), from -inf (silent) to +6 dB. Balances range from
  -1.0 to +1.0.
- Signal path: a mono physical track has mono inserts and is then panned to stereo with
  constant-power panning (-3 dB at centre). A stereo WAV track has stereo inserts, and balance
  sets its left/right level. Master chains are stereo. The metronome and info clicks are mono,
  centred.
- A bus is mono when the second value of `m_outputs` is -1.
- A song has at most `maxNumberOfTrackPerSong` (16) tracks in total: audio, physical and info.

### Timing and transport

- Tempos are whole-number BPM values that change instantly. BPM counts quarter notes in every
  time signature (at 120 BPM in 6/8, an eighth note lasts 0.25 s), which matches the 960 PPQ maths.
- `DEFAULT_TOTAL_BAR` is 32, so `m_rightLocator` defaults to `{ 32, 1, 1, 0 }` (the start of the
  last bar). Both locators can be set by the user.
- When the furthest WAV end moves after `m_rightLocator` (a WAV is inserted or moved, or a tempo
  change shifts where a WAV ends in bars), `m_rightLocator` moves to that end and
  `m_totalNbOfBars` grows to include it. It never moves earlier on its own, for example when a
  WAV is removed.
- `m_rightLocator` is where the song ends; it then stops or goes on to the next song, depending
  on `m_endSongAction`. WAV audio past it is cut with a 10 ms fade-out. After the last song of a
  playlist, playback stops, even with `Continue`.
- `m_endStopDelay` is in seconds, capped at 30, and comes before the next song's count-in. When
  the next song's metronome is `Off`, there is no count-in. `stop()` returns the playhead to `m_leftLocator`. `pause()` fades all
  tracks out over 10 ms and keeps the position; `play()` after a pause fades back in.
- The metronome clicks every beat-unit (every eighth note in 6/8), with beat 1 accented. Count-ins
  are 4 beat-units long. Info clicks come in groups of the bar's numerator, with beat 1 accented,
  and sound different from the metronome.
- Between songs, the only sound allowed is the 4-click count-in before the next `m_leftLocator`,
  when the metronome is `On` or `FourCountdownOnly`.

### Live inputs

- Physical inputs are mono, and bank-slot chains are mono in, mono out. Live inputs are silent
  while the transport is stopped or paused (a tuning mode will come later).
- The DI output is the mono physical input after its bank-slot chain, before the mixers.
  `m_DIOutput == -1` means no DI output.
- Every plugin chain is created, prepared and warmed up while the playlist loads, so the first
  switch to a slot in a session makes no buzz or click. During a `Continue` change, an input
  switches to the next song's slot at the right locator with a short crossfade, and the old
  chain's tail rings out.
- Before the first automation point, a physical track uses the first slot in its `m_bankSlots`.
- Plugin latency is compensated, and effect tails keep ringing after a slot switch.

### Storage

- Songs belong to one playlist. A song used in two playlists is stored twice.
- Selecting a song in another playlist unloads the current playlist and loads the new one.
- Effects tuning received over TCP (bank slot effects, and mix inserts, volume and balance) is
  saved to the data files about 1 s after the last change, through a temporary file that then
  replaces the original.
- The data folder defaults to `<user home>/OneBox`, and `ONE_BOX_DATA_DIR` overrides it.

### Audio device and plugins

- The engine opens the system's default input and output devices by default. `MBoxCoreAudio`
  provides getters and setters for the device and block size, and lists the available devices.
- The UI scans for plugins and saves the list. The rack only reads that list. The UI and the rack
  run on the same computer for now.
- A device or block size the user selects is saved in `rig.json` and reopened at the next launch,
  falling back to the system default when that device isn't connected.

### Protocol and tests

- The TCP protocol is newline-delimited JSON (JSON-RPC style requests, responses and events).
- Tests use JUCE's `UnitTest`.

## Where the code differs from the diagram

Implemented 2026-11-20. Names follow the diagram except where listed here.

- **Public API types.** `MBoxCoreAudio` takes and returns `std::string`, `std::vector` and small
  structs (`MBoxResult` instead of `juce::Result`), so the shared library's header stays free of
  JUCE (CLAUDE.md rule). `getSongInstance` and `getAudioEngine` exist but are only usable by
  apps that compile the library in. After editing a song directly, the UI calls
  `songChanged (playlist, song)` (likewise `rigChanged()`, `banksChanged()`).
- **`IAudioEffectInstance` / `IAudioEffect`** became `MBoxEffect`: the stored description of one
  plugin (identity, base64 state, bypass). Running plugins live in `MBoxEffectChain`s built at
  playlist load.
- **`std::list<IAudioEffectInstance>` in `MBoxBankSlot`** is a `std::vector<MBoxEffect>`.
- **`std::array<MBoxBankSlot, NB_OF_BANKS>`** is sized by `numberOfSlotsPerBanks`.
- **`std::pair<Bar, BankSlot*>`** in `MBoxPhysicalTrack` is `std::pair<Bar, MBoxSlotRef>` (1-based
  bank and slot numbers), so it can be saved.
- **`MBoxTrackMix::m_track`** is `m_trackId`; track mixes are owned by `MBoxSong::m_mixes`, not by
  `MBoxMixBus`.
- **`MBoxMixBus::m_metronome`** keeps only the volume; the state is `MBoxSong::m_metronomeState`.
- **`MBoxAudioTrack::m_clipStreamer`** and **`MBoxAudioEngine::m_readAheadThread`** are gone: WAVs
  are decoded at load into `MBoxAudioClipCache` and played by the renderer from
  `MBoxSongPlayer::Track` (the clip streamer's role), with no disk access during playback.
  `MBoxAudioTrack` also stores the WAV's length, to move the right locator.
- **`rename (name, newName)`** on `MBoxPlaylist` / `MBoxSong` is `rename (newName)` on the playlist
  and `renameSong (name, newName)` on the playlist for songs.
- **`MBoxSong (const std::string&)`** and the `std::string` names use `juce::String`.
- **`DEFAULT_TOTAL_BAR`** is `defaultTotalBar` (32), following the constant naming of the design
  document.
- **`MBoxAudioEngine::m_mixBuses`** live in `MBoxRig` (saved in `rig.json`); the renderer gets
  their values through `MBoxRenderer::setBus`.
- **Added:** `MBoxTimeline`, `MBoxRig`, `MBoxDeviceSettings`, `MBoxSlotRef`, `MBoxStorage`
  (the diagram's `MBoxStorage`), `MBoxRenderer`, `MBoxSongPlayer` / `MBoxPlaylistPlayer`,
  `MBoxPluginHost`, `MBoxLiveInput`, `MBoxServer`.

## Not yet detailed in the diagram

`MBoxStorage`, `MBoxServer`, `MBoxClipStreamer` (extends `juce::TimeSliceClient`), `IAudioEffect`.
These are now designed in the code: see the headers in `Sources/Persistence`, `Sources/TCPServer`,
`Sources/Audio` and `Sources/Plugins`.
