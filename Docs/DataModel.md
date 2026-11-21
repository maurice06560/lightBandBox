# MBox data model

The data model as implemented in `Sources/Model`, how it is saved, and the runtime objects the
engine builds from it. It started from the Lucidchart "One Box Band" diagram; the code is now
the reference, and this document follows it. The headers have the details of every method.

## Constants (`MBoxTypes.h`)

```cpp
constexpr int numberOfMixBus          = 3;
constexpr int numberOfBanks           = 5;
constexpr int numberOfSlotsPerBanks   = 5;
constexpr int maxNumberOfTrackPerSong = 16;     // audio, physical and info tracks together

constexpr int defaultTotalBar        = 32;
constexpr int defaultTempo           = 120;     // BPM
constexpr int minTempo               = 20;
constexpr int maxTempo               = 400;
constexpr int maxEndSongDelaySeconds = 30;

constexpr int ticksPerQuarterNote = 960;
constexpr int ticksPerDivision    = 240;        // a division is a sixteenth note
constexpr int countInBeats        = 4;

constexpr double supportedSampleRate  = 44100.0; // the only rate, for WAVs and the engine
constexpr double minVolumeDb          = -100.0;  // at or below: silent
constexpr double maxVolumeDb          = 6.0;
constexpr double transportFadeSeconds = 0.010;   // play, pause, stop and locator fades
```

## Ownership

```
MBoxCoreAudio (exported; std:: types only)
└── Impl
    ├── MBoxStorage                      the JSON data folder
    ├── MBoxPlaylist (n)                 ordered, names unique ignoring case
    │   └── MBoxSong (n)                 ordered, names unique ignoring case
    │       ├── MBoxTrack (n, max 16)    MBoxAudioTrack | MBoxPhysicalTrack | MBoxInfoTrack
    │       └── MBoxTrackMix (one per track, per mix bus)  ──by m_trackId──> MBoxTrack
    ├── MBoxRig                          rack-wide
    │   ├── MBoxMixBus (3)               outputs, master section, MBoxMetronome
    │   └── MBoxDeviceSettings
    ├── MBoxBanks = MBoxBank (5)         rack-wide
    │   └── MBoxBankSlot (5)             a mono effect chain: MBoxEffect (n)
    ├── MBoxPluginHost, MBoxAudioClipCache, MBoxAudioEngine (MBoxRenderer), MBoxServer
    └── the loaded playlist's runtime player (see "Runtime objects")

MBoxPhysicalTrack ── slot automation (Bar, MBoxSlotRef) ──> MBoxBankSlot
```

Model classes are plain data, edited on the message thread. The audio thread never reads them:
the engine builds runtime objects from a copy (see below).

## Value types

```cpp
struct Bar                  // a position {bar, beat-unit, division, tick}
{
    int m_bar      { 1 };   // from 1
    int m_beat     { 1 };   // from 1; a beat-unit is one denominator unit (an eighth in 6/8)
    int m_division { 1 };   // from 1; a sixteenth note
    int m_tick     { 0 };   // 0 to 239
};                          // written "bar.beat.division.tick", e.g. "12.3.1.0"

struct Signature            // denominators 2, 4, 8 or 16; numerators 1 to 32
{
    int m_numerator   { 4 };
    int m_denominator { 4 };
};

enum class EndSongAction  { Stop, Continue };
enum class MetronomeState { Off, FourCountdownOnly, On };
enum class TrackType      { Audio, Physical, Info };
enum class TransportState { Stopped, Playing, Paused };
```

The enums are written in the files and the protocol by name: `"stop"`, `"continue"`, `"off"`,
`"fourCountdownOnly"`, `"on"`...

`MBoxTimeline` converts between `Bar`, absolute ticks, seconds and sample frames from a song's
grids. Frame 0 is the start of bar 1; every frame position is computed from there and rounded
once, so no error builds up. It is immutable, so the audio thread can share it.

## MBoxPlaylist

```cpp
juce::String                           m_name;
std::vector<std::unique_ptr<MBoxSong>> m_songs;
```

`addSong`, `removeSong`, `renameSong`, `moveSong` (0-based), `rename`, `getSongInstance`,
`getSongIndex`. Playlist and song names are folder names: `validateItemName()` rejects empty or
over 64 characters, characters invalid on Windows, macOS or Linux, leading or trailing spaces,
a trailing dot and reserved Windows names. A song belongs to one playlist; `copyPlaylist` makes
deep copies, WAV files included.

## MBoxSong

```cpp
juce::String                                           m_name;
std::vector<std::pair<Bar, int>>                       m_tempoGrid;      // whole BPM
std::vector<std::pair<Bar, Signature>>                 m_signatureGrid;
int                                                    m_totalNbOfBars  { defaultTotalBar };
Bar                                                    m_leftLocator    { 1, 1, 1, 0 };
Bar                                                    m_rightLocator   { defaultTotalBar, 1, 1, 0 };
std::vector<std::unique_ptr<MBoxTrack>>                m_tracks;
EndSongAction                                          m_endSongAction  { EndSongAction::Stop };
int                                                    m_endStopDelay   { 0 };   // seconds, 0 to 30
MetronomeState                                         m_metronomeState { MetronomeState::Off };
std::array<std::vector<MBoxTrackMix>, numberOfMixBus>  m_mixes;          // per bus, in track order
```

Invariants kept by its methods:
- Both grids always have an entry at bar 1 and are sorted. Signatures change only at the start
  of a bar. Tempos change instantly; BPM always counts quarter notes (at 120 BPM in 6/8, an
  eighth note lasts 0.25 s).
- Every stored position is valid for the signature of its bar. After a signature change,
  out-of-range beats and divisions are clamped to the last valid one.
- `m_leftLocator < m_rightLocator`, and the right locator's bar is at most `m_totalNbOfBars`.
- When the end of a WAV moves after the right locator (a WAV is set or moved, or a tempo change
  shifts where it ends), the right locator moves to that end and the bar count grows to include
  it. It never moves earlier on its own.
- Every bus has exactly one `MBoxTrackMix` per track. Adding a track gives it the next free id
  and a default mix in every bus; removing it removes its mixes.

## Tracks

```cpp
class MBoxTrack                      // abstract; getType(), clone()
{
    int          m_id { 1 };         // unique in its song; how mixes and the protocol refer to it
    juce::String m_name;
};

class MBoxAudioTrack : public MBoxTrack        // a 44100 Hz, 16-bit stereo WAV
{
    Bar          m_audioStart { 1, 1, 1, 0 };
    juce::File   m_filename;                   // in the song's media folder
    std::int64_t m_lengthInFrames { 0 };       // read from the WAV header, to move the right locator
};

class MBoxPhysicalTrack : public MBoxTrack     // a live mono input
{
    int m_physicalInput { 0 };                 // 0-based input channel
    int m_DIOutput { -1 };                     // 0-based output channel, -1 for none
    std::vector<std::pair<Bar, MBoxSlotRef>> m_bankSlots;   // slot automation, sorted
};

class MBoxInfoTrack : public MBoxTrack         // cue clicks
{
    std::vector<Bar> m_infoClicks;             // each starts a group of clicks, sorted
};
```

- A physical track's input goes through the bank slot active at the playhead: the last entry
  at or before it, or the first entry before any. Then to its DI output (if any), then to the
  mixes.
- An info cue clicks once per beat-unit, as many times as the numerator of its bar's signature,
  the first click accented. Cue clicks outside the locators don't sound.

## Mixes

```cpp
struct MBoxTrackMix                  // one track in one bus, for one song (in song.json)
{
    int                     m_trackId { 1 };
    double                  m_volume  { 0.0 };   // dB, minVolumeDb (silent) to maxVolumeDb
    double                  m_balance { 0.0 };   // -1.0 (left) to +1.0 (right)
    bool                    m_mute { false };
    bool                    m_solo { false };    // solos add up within a bus
    std::vector<MBoxEffect> m_inserts;
};

class MBoxMixBus                     // rack-wide (in rig.json); id 1 to 3
{
    int                     m_busId { 1 };
    juce::String            m_name;
    std::vector<MBoxEffect> m_masterEffectChain;   // stereo
    double                  m_masterVolume  { 0.0 };
    double                  m_masterBalance { 0.0 };
    std::pair<int, int>     m_outputs { 0, 1 };    // 0-based; mono when second is -1, silent when first is -1
    MBoxMetronome           m_metronome;
};

struct MBoxMetronome
{
    double m_volume { 0.0 };   // dB
    bool   m_on { true };      // whether the click (count-in included) plays in this bus at all
};
```

- Whether the metronome clicks is per song (`MBoxSong::m_metronomeState`), shared by all buses.
  Its level and whether a bus hears it are per bus. There is no tone setting.
- Signal path: a physical track is mono through mono inserts, then panned to stereo with
  constant-power panning (-3 dB at centre). An audio track is stereo through stereo inserts,
  and balance sets its left/right levels. The metronome and info clicks are mono and centred.
  Each bus then runs its master chain, master volume and balance.

## Banks and effects

```cpp
struct MBoxEffect                    // one plugin, as stored
{
    juce::String      m_name, m_format /* "VST3" or "AudioUnit" */, m_manufacturer;
    juce::String      m_identifier;  // a lookup hint only: never a path, which differs per machine
    int               m_uniqueId { 0 };
    juce::String      m_version;
    juce::MemoryBlock m_state;       // getStateInformation(), base64 in the files
    bool              m_bypassed { false };
};

struct MBoxBankSlot { juce::String m_name; std::vector<MBoxEffect> m_effectChain; };   // mono in, mono out
struct MBoxBank     { juce::String m_name; std::array<MBoxBankSlot, numberOfSlotsPerBanks> m_bankSlots; };
using  MBoxBanks  = std::array<MBoxBank, numberOfBanks>;                                 // rack-wide
struct MBoxSlotRef  { int m_bank { 1 }; int m_slot { 1 }; };                             // 1-based
```

Plugins can be AU (macOS only) or VST3. The UI scans for plugins and saves the list; the rack
only reads it.

## Rig

```cpp
struct MBoxRig
{
    std::array<MBoxMixBus, numberOfMixBus> m_mixBuses;   // m_mixBuses[i] has id i + 1
    MBoxDeviceSettings                     m_device;
};

struct MBoxDeviceSettings            // empty names: the system default device
{
    juce::String m_typeName, m_inputDeviceName, m_outputDeviceName;
    int          m_bufferSize { 0 }; // 0: the preferred size (32 where supported)
};
```

A device that isn't connected at startup falls back to the system default.

## Storage (`MBoxStorage`)

```
<root>/banks.json
<root>/rig.json
<root>/playlists/playlists.json                                  playlist and song order
<root>/playlists/<Playlist>/songs/<Song>/song.json               grids, locators, end, metronome, mixes
<root>/playlists/<Playlist>/songs/<Song>/tracks/track.json       the tracks
<root>/playlists/<Playlist>/songs/<Song>/tracks/media/*.wav      the WAVs (relative paths in track.json)
```

- The root is `ONE_BOX_DATA_DIR`, or `<user home>/OneBox`.
- Every file has a `schemaVersion` (1). Every write goes through a temporary file that then
  replaces the original. Positions are `"bar.beat.division.tick"` strings.
- Changes are saved about 1 s after the last one, including plugin settings changed over TCP
  or in plugin editors.
- Removed playlists and songs are moved to `<root>/.trash`, since their folders hold WAVs.
  Renaming a playlist or song renames its folder at once.

## Editing through `MBoxCoreAudio`

The exported API uses `std::` types and `MBoxResult` only. Embedded apps (the UI) can also edit
the model directly (`getSongInstance`, `getRig`, `getBanks`), then tell the library what
changed, so it saves and updates the running player:

| Call | After changing | Effect on the loaded playlist |
|---|---|---|
| `songChanged (playlist, song)` | one song: tracks, cues, locators, grids, inserts, song end | rebuilds that song only (see below) |
| `metronomeChanged (playlist, song)` | a song's metronome state | applied live, no rebuild |
| `rigChanged()` | buses, master chains, device | rebuilds the playlist |
| `banksChanged()` | banks and slot chains | rebuilds the playlist |
| `effectChanged (target, effect)` | a running plugin's settings | saved; no rebuild |

Live values (track volume, balance, mute, solo; bus master volume and balance; metronome volume
and on/off per bus; effect bypass and parameters) go to the model and the running player at
once.

## Runtime objects (`Sources/Audio/MBoxSongPlayer.h`)

When a playlist is selected, a loader thread builds its runtime version from a copy of the
model, and the renderer swaps it in with one atomic pointer between audio blocks.

```cpp
struct MBoxPlaylistPlayer
{
    std::vector<std::shared_ptr<MBoxSongPlayer>> m_songs;
    std::map<std::pair<int, int>, std::shared_ptr<MBoxLiveInput>> m_liveInputs;   // (input, n-th on it in a song)
    std::array<std::shared_ptr<MBoxEffectChain>, numberOfMixBus> m_masterChains;
    int m_maxSlotLatency, m_maxInsertLatency, m_totalLatency;
};

struct MBoxSongPlayer
{
    MBoxTimeline m_timeline;
    std::int64_t m_leftFrame, m_rightFrame, m_countInFrames, m_endDelayFrames;
    EndSongAction m_endSongAction;
    std::vector<MBoxClickEvent> m_countInClicks, m_metronomeClicks;
    std::atomic<MetronomeState> m_metronomeState;          // live
    std::vector<Track> m_tracks;   // per track: the decoded clip or live input and slot changes,
                                   // the info clicks, and per bus its inserts (MBoxEffectChain)
                                   // and mix state (atomics)
};
```

- WAVs are decoded once into `MBoxAudioClipCache` (kept as 16-bit) and played from memory: no
  disk access while playing. The transport's `int64` frame counter is the only position.
- Every plugin chain is created (on the message thread), prepared and warmed up at load, so the
  first use in a show makes no noise. Plugin latency is compensated: every path (WAVs, live
  inputs, clicks) is padded to `m_totalLatency`.
- Live inputs are shared by all the songs, so an instrument's chain keeps running across songs.
  A slot switch crossfades over 10 ms, and the old chain's tail rings out.
- `songChanged` rebuilds only that song's player, sharing the other songs, the live inputs and
  the master chains with the running player. Tracks keep their running inserts and mix state
  when their plugins are unchanged (same plugins, order and bypass). It falls back to a full
  rebuild when a new chain is longer than the playlist's latency, or a physical track needs a
  bank slot its live input doesn't run yet.

## Transport rules

- Selecting a song of another playlist unloads the current one and loads the new one; within
  the loaded playlist, it stops (with a fade) at the song's left locator.
- `play()` from a stop starts at the left locator, after the count-in when the metronome isn't
  `Off`. `playFrom()` starts anywhere (count-in only at the left locator). `pause()` keeps the
  position, `stop()` returns to the left locator. All fade over 10 ms.
- WAVs fade in over 10 ms from the left locator and out over 10 ms up to the right locator.
- A song ends once its right locator has been heard (latency included). Then it stops, or with
  `Continue` goes on to the next song after `m_endStopDelay` seconds and that song's count-in.
  After the last song, playback stops. Between songs, only the count-in clicks sound.
- The metronome clicks every beat-unit, beat 1 accented; the count-in is 4 beat-units at the
  left locator's tempo and signature. Info clicks sound different from the metronome.
- Live inputs only sound while playing.
