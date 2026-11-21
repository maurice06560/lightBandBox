# MBox implementation plan

Approved and implemented 2026-11-20 (see "Status" at the end). It builds on `Docs/DataModel.md` (the diagram plus every
decision taken since) and the design document `OneBox-Full-designV1.docx`.

Each phase ends with a build on macOS, Windows and Linux and its unit tests passing. Phases 1 and 2
need no audio device; later phases keep the audio processing testable without one (see "Testing").

## Threads

| Thread | Runs | Must never |
|---|---|---|
| Message thread | the public API, model edits, storage, TCP request handling, event broadcast | block for long (a playlist load runs elsewhere) |
| Loader pool | WAV decoding, plugin creation, preparation and warm-up, runtime rebuilds | touch the model while the message thread edits it (it works on a copy) |
| Audio thread | `audioDeviceIOCallbackWithContext`: rendering, transport, fades | allocate, lock, log, do I/O, or wait |
| TCP thread | socket reads and writes | call the engine directly (requests are passed to the message thread) |

Commands go from the message thread to the audio thread through a lock-free queue (`juce::AbstractFifo`).
Events go back the same way, in the other direction. Larger changes are complete runtime snapshots
swapped through one atomic pointer; the old snapshot is freed on the message thread.

The headless host calls a blocking `MBoxCoreAudio::run()` that runs JUCE's message loop. The JUCE
UI app already runs it.

## Phase 0: Build and project layout

- C++20. Move `MBoxCoreAudio.{h,cpp}` to `Sources/` (the layout in the design document), and add
  `Sources/Plugins/` and `Tests/`.
- Two library flavours from the same sources:
  - `MBoxCore` (shared) for the headless rack. JUCE is compiled in and hidden, as now.
  - `MBoxCoreStatic` for the JUCE UI app. Here the JUCE modules must reach the app as usage
    requirements rather than being compiled into the library, so the app compiles JUCE exactly
    once. The public header may then use JUCE types (`juce::Result`, `juce::String`) safely.
- Windows: `JUCE_ASIO=1`, with the ASIO SDK path given as a CMake option (`MBOX_ASIO_SDK_DIR`).
  The SDK is not committed.
- Linux: check the system packages listed in `JUCE/docs/Linux Dependencies.md`.
- Evaluate JUCE 9's `juce_audio_processors_headless` module for the rack build (plugin hosting
  without GUI dependencies).
- `Tests/`: a console app running JUCE `UnitTest`s, linked against `MBoxCoreStatic`, run by CTest.
- Recommended: `git init` the top level, with JUCE as a submodule (as in the design document), and
  a CI matrix for the three OSes.

## Phase 1: Timing model

- `Bar`, `Signature`, and `MBoxTimeline`, built from a song's tempo and signature grids.
- Conversions: `Bar` ↔ absolute ticks (signature map, 960 PPQ) ↔ seconds (tempo map, quarter-note
  BPM) ↔ `int64` sample frames at 44100 Hz. Frames are computed from the start of the song and
  rounded once (to the nearest frame, halves rounded up). Tick durations are never accumulated.
- Validation: signatures only at the start of a bar, denominators 2, 4, 8 or 16, beat and
  division within range, ticks 0 to 239, a tempo and a signature always present at bar 1.
- Tests: every worked example in the design document (120 → 180 BPM, 4/4 → 2/4, the
  22.96875-frames-per-tick case), and round trips for 3/4, 6/8 and 7/8.

## Phase 2: Model and storage

- Model classes as plain data: `MBoxPlaylist`, `MBoxSong`, `MBoxTrack` with `MBoxAudioTrack`,
  `MBoxPhysicalTrack` and `MBoxInfoTrack`, `MBoxTrackMix`, `MBoxMixBus` (global part),
  `MBoxBank`, `MBoxBankSlot`, and an effect descriptor (plugin identity + base64 state + bypass).
- Rules enforced by the model: the 16-track limit, unique names that are valid folder names on
  every OS, locators within the song, and the right locator moving later automatically.
- `MBoxStorage`: reads and writes `banks.json`, `rig.json`, `playlists/playlists.json`,
  `song.json` and `track.json`. Every file carries a `schemaVersion`. Writes go to a temporary
  file that then replaces the original. Changes from TCP are saved about 1 s after the last one.
  Data folder: `ONE_BOX_DATA_DIR`, or `<user home>/OneBox`.
- `MBoxCoreAudio` playlist and song API from the diagram. A second instance, in this process or
  another, fails cleanly (`juce::InterProcessLock` on the data folder).
- Tests: save then load gives an identical model, rule violations are rejected, a missing file
  gives a clear error.

## Phase 3: Engine core

- `MBoxAudioEngine::initialise()` returns a `juce::Result` instead of opening the device in the
  constructor.
- Device API on `MBoxCoreAudio`: list device types and device names, select a device and block
  size, save the choice in `rig.json`, fall back to the system default.
- The renderer is separate from the device callback, so tests can drive it with plain buffers.
- Transport: `int64` frame position; stopped, playing and paused states; 10 ms fades on pause,
  resume, stop and at the right locator; `stop()` returns to the left locator.
- Buses: summing, routing to output channels (mono when the second output is -1), master volume
  and balance, smoothed gain changes.
- Metronome and info clicks: synthesised, beat 1 accented, info clicks with a different sound,
  4-beat-unit count-in when the song's metronome is `On` or `FourCountdownOnly`.
- Tests: render offline and check click positions to the exact frame, fade shapes, and that
  stopping returns to the left locator.

## Phase 4: Playlist loading and WAV playback

- Playlist loading on the loader pool, with progress events. Selecting a song in another playlist
  unloads the current one first.
- `MBoxAudioClipCache`: every WAV decoded once and kept as 16-bit. Files that aren't 44.1 kHz,
  16-bit stereo are rejected with their name. The total size is checked against free RAM before
  loading.
- `MBoxSongPlayer` and `MBoxClipStreamer` as described in `DataModel.md`.
- End of song: `Stop`, or `Continue` with `m_endStopDelay` seconds (up to 30), then the count-in,
  then the next song from its left locator. Playback stops after the last song.
- Tests: two songs chained with `Continue` render with the expected gap, frame for frame.

## Phase 5: Plugin hosting

- The plugin list: the UI scans and saves it, and the rack reads it. VST3 on every OS, AU on
  macOS only. Plugins are identified by format, name, manufacturer and ID, never by path.
- Bank-slot chains, mono in and mono out, one instance per (physical input, slot) pair the
  playlist uses. Effect inserts per track and bus for each song. Stereo master chains per bus.
- Every chain is created, prepared and warmed up (a few seconds of silence processed off the
  audio thread) during playlist loading.
- Slot automation: switch with a short crossfade, and keep the old chain running until its tail
  ends (`getTailLengthSeconds`, with a cap).
- Latency compensation: everything is delayed to match the highest latency among the loaded
  chains, so switching slots never shifts timing.
- DI output: the mono chain output, before the mixers. Live inputs are silent while stopped or
  paused.
- Parameters are addressed by `getParameterID()` with normalised values from 0 to 1.

## Phase 6: Mixer controls

- Volume (-inf to +6 dB), balance, mute and solo for each track mix, with smoothing.
- Solos add up within a bus. Constant-power panning for mono tracks.

## Phase 7: TCP server

- Newline-delimited JSON, port `ONE_BOX_DATA_TCP_PORT` or 5050. `MBoxComAPI.h` is replaced by the
  method and event definitions below.
- Rules: a `hello` with the protocol version first, then a `ping` every second, messages up to
  64 KB, several clients, transport events limited to 20 per second.

### Draft methods

| Method | Params | Result |
|---|---|---|
| `hello` | `protocolVersion`, `client` | `protocolVersion`, `server` |
| `ping` | | |
| `getState` | | transport state, playlist, song, position, tempo, signature |
| `getPlaylists` | | names |
| `getSongs` | `playlist` | names |
| `selectSong` | `playlist`, `song` | (loading continues with events) |
| `play`, `pause`, `stop` | | |
| `getMix` | `bus` | tracks with volume, balance, mute, solo |
| `setTrackVolume` / `setTrackBalance` / `setTrackMute` / `setTrackSolo` | `bus`, `track`, value | |
| `setBusVolume` / `setBusBalance` | `bus`, value | |
| `getEffects` | `target` | effect names and bypass states |
| `getEffectParameters` | `target`, `effect` | IDs, names, values |
| `setEffectParameter` | `target`, `effect`, `parameterId`, `value` | |
| `setEffectBypass` | `target`, `effect`, `bypassed` | |

`target` is `{"slot":{"bank":b,"slot":s}}`, `{"insert":{"bus":b,"track":t}}` or `{"master":{"bus":b}}`.

### Draft events

`transport`, `playlistLoading` (progress), `playlistLoaded` (result), `songChanged`,
`slotChanged`, `mixChanged`, `deviceChanged`, `error`.

### Errors

`{"code": n, "message": "..."}` with codes for: unknown method, bad parameters, not found,
not allowed in the current state, busy loading, internal error.

## Phase 8: Hardening

- Watch for audio dropouts and CPU load, and report them as events.
- Multi-hour run of a full playlist in a loop on the rack hardware.
- Validate ASIO at 32-sample buffers on the target interface.

## Conventions I will follow unless told otherwise

- Bus, bank, slot and track numbers are 1-based in the API and the protocol, like `m_busId { 1 }`.
  Positions in `move…(…, int to)` are 0-based list indexes.
- The public API reports failures through `juce::Result` and never throws.

## Outside the code

- JUCE is dual-licensed AGPLv3 / commercial; a closed-source rack product needs a commercial
  JUCE licence. The ASIO SDK has its own licence from Steinberg.
- Commercial plugins (for example Neural DSP) need their activation to work on an offline rack.

## Status (2026-11-20)

Phases 0 to 7 are implemented and covered by 42 unit test groups (`Tests/`), which pass on
macOS with no warnings from the project's code. A JUCE-free program linking the shared library
was also run against the real default CoreAudio device (44100 Hz, 32-sample buffers).

| Phase | State | Notes |
|---|---|---|
| 0 Build | Done | C++20, `MBoxCoreSources` / `MBoxCore` / `MBoxCoreEmbedded`, ASIO option, `MBoxCoreTests` + CTest. Not done: `git init`, CI, a Windows or Linux build. |
| 1 Timing | Done | All design-document examples are tests. |
| 2 Model and storage | Done | |
| 3 Engine core | Done | Device API, null device, renderer, clicks. |
| 4 Playback | Done | |
| 5 Plugins | Done, partly verified | Tested with Apple's AUDelay and with empty chains. Not yet tried: VST3s, Neural DSP plugins, real latency values, the plugin scanner. |
| 6 Mixer controls | Done | |
| 7 TCP server | Done | Tested through a real socket on localhost. |
| 8 Hardening | Not started | Needs the rack hardware. |

Known limitations:
- Slot changes take effect at the start of the audio block containing them (the device block, at most 256 frames), not on their exact frame: up to 0.7 ms late at 32-sample buffers.
- Changing the metronome state, a plugin chain, or a song's structure rebuilds the whole loaded
  playlist in the background (plugins included, so it takes seconds); playback continues with
  the previous version until the new one is swapped in.
- When pausing, the playhead keeps moving during the 10 ms fade, so it stops about 441 frames later.
- Removing the loaded playlist while it plays cuts the audio without a fade (a setup operation).
- The DI output doesn't include the master chains' latency, so it's early by that amount when
  master chains add latency.
- If the device can't run at 44100 Hz, the engine still opens it (logging a warning) and WAVs
  play at the wrong speed.

## Phase 9: JUCE UI port

Port the previous implementation's UI (`Applications/UI`, `Modules/onebox_ui`) to `MBoxCoreAudio`,
linking `MBoxCoreEmbedded`. Analysis, decisions and order: `Docs/UIPortPlan.md`. Steps 1 to 4
done (the tuner and the bank-slot loudness are postponed).
