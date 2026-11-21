# Porting the JUCE UI to MBoxCoreAudio

The UI of the previous implementation (`Applications/UI`, built on the `Modules/onebox_ui` JUCE
module) talked to `MBoxApplication`. This plan ports it to `MBoxCoreAudio`. Status: steps 1 to 4
done (2026-11-20); step 5 (tuner, loudness) postponed.

## Decisions (2026-11-20)

| Question | Decision |
|---|---|
| How the UI links the library | `MBoxCoreEmbedded` (compiled in, one copy of JUCE). The shared library can't serve the UI: model classes aren't exported, JUCE objects can't cross between two JUCE copies, and plugin editors from the library's JUCE can't sit in the app's windows. |
| Metronome tone | Removed. |
| Metronome on/off | Per mix, rack-wide (see "Model changes" below). |
| Input monitoring while stopped (`isMonitored`) | Dropped for now: the monitor button goes. |
| Built-in, non-plugin effects (`IAudioEffect`, `AudioEffectFactory`) | Dropped: chains hold AU / VST3 plugins only. |
| Tuner, bank-slot loudness (LUFS) | Postponed: the UI keeps them hidden until the engine has them. |

## What is reused

- `Modules/onebox_ui`: reused as is (depends only on `juce_gui_basics`, no model code). Its
  ChannelFader shows silence as -inf dB; the model's silence is `minVolumeDb` (-100 dB), so the
  UI converts at the boundary.
- `Applications/UI`: the panels are kept; their model code is ported. `PluginTest` (marked
  temporary) is deleted. A `Main.cpp` (`JUCEApplication` owning `MBoxCoreAudio`, `Session` and the
  window) and a CMake target are new.

## Model changes

- **Metronome on/off per mix, rack-wide.** Each mix bus (`MBoxMixBus`, saved in `rig.json`)
  gets, next to the metronome volume, whether the click plays in it (on by default). The
  `MetronomeState` stays per song. A bus with the click off hears neither the count-in nor the
  clicks. Exposed through the API (`setMetronomeInMix (bus, on)`) and the TCP protocol.

## Old to new mapping

| Old (`MBoxApplication`) | New |
|---|---|
| Per-song mixes (`MBoxSubmix`) with an `MBoxMaster` (outputs, volume, balance, inserts) | Track mixes per song (`MBoxTrackMix`); the master is rack-wide in `MBoxRig::m_mixBuses` (edit + `rigChanged()`) |
| Mix index, 0-based | Bus, 1-based |
| Balance -64..+63 | -1.0..+1.0 |
| `MBoxTrack` + `getTrackType()` (Wav, Physical) | `MBoxAudioTrack`, `MBoxPhysicalTrack`, `MBoxInfoTrack` (info tracks are new to the UI) |
| `getBankSlots` / `setBankSlots`, `BankSlotRef` | `getBankSlotChanges` / `setBankSlotChange`, `MBoxSlotRef` (1-based) |
| DI send `MBoxTrack::Outputs` | `int` DI output (mono) |
| `getStartBar` / `getEndBar`, `getTotalBars` | Left / right locators, `getTotalBar` |
| `EndAction`, `nextDelay` | `EndSongAction`, `EndSongDelay` (0 to 30 s) |
| `TempoMap`, `TempoLane`, `TempoTable` | `MBoxTimeline` |
| Live effect instances in the model (`MAudioEffect`) | `MBoxEffect` descriptors; live plugins exist only in the loaded player |
| Explicit save, dirty tracking | Autosave about 1 s after `songChanged()` / `rigChanged()` / `banksChanged()` |
| `loadFolder()` | Data folder fixed at construction (dropped, or recreate `MBoxCoreAudio`) |
| `copyPlaylist` | Not in the API yet |
| `preparePlayback`, `preloadOneInsert`, `computeLatencies`, `songReachedEnd` auto-next | Done by the library (background playlist load, latency padding, `EndSongAction::Continue`): removed from `Session` |

## Library features the UI needs

1. Plugin editor windows: access to the running `AudioPluginInstance` of a slot, insert or
   master effect (embedded only), and writing its state back to `MBoxEffect::m_state`.
2. Play from a position (the edit cursor): a locate command in the renderer.
3. Meters: track, bus master and metronome peaks, written by the renderer, read by a UI timer.
4. Count-in state for the transport bar (counting in, current beat).
5. Input and output channel names for the routing pickers.
6. Metronome on/off per mix (above).
7. Playlist copy.
8. Later: tuner, bank-slot loudness, bank-slot monitoring.

## Order

1. CMake target `Applications/UI` (`juce_add_gui_app`, `MBoxCoreEmbedded`, `onebox_ui`) and
   `Main.cpp`. Done: the `OneBoxBand` app starts the library behind the splash and shows a
   temporary placeholder window.
2. Rewrite `Session` against `MBoxCoreAudio`: selection, edits through the `...Changed()`
   methods, transport, events through `addEventListener`. Remove dirty tracking, preloading,
   latency and auto-next. Done, tested by `Tests/UISessionTests.cpp` (in `MBoxCoreTests`):
   - Selecting a song selects it in the library (which loads its playlist). `songChanged`
     events move the selection (Continue, TCP clients), except while waiting for our own load.
   - Live mixing needs the selected song loaded (`isSelectionLoaded`); bus settings don't.
   - Edits stay locked while playing or paused, except live mixing, metronome and song end.
   - `play()` starts from the left locator until the library can locate (step 4).
   - New library call `importAudioFile`: copies a WAV into the song's media folder.
   - Dropped with the old model: dirty tracking and per-song save, playlist copy / paste,
     load folder, monitoring, latency, bank-slot loudness, plugin-window change marks
     (back in step 4). The original is in `Applications/UI/Legacy/` until step 3 ends.
3. Port the panels: Browser, Transport, ListEditor, Arrangement, Submix (rig), Mixer. Done,
   with the Audio device window. The window matches the previous UI. Differences:
   - The arrangement's positions come from a UI `TempoMap` (beats = quarter notes) built on
     `MBoxTimeline`; the playhead reads the renderer's frame position. START and END are the
     left and right locators; the canvas runs to the song's last bar.
   - Info (cue click) tracks are shown, added from "+" and edited by double-clicking their lane.
   - The Song tab edits the locators and the bar count; the metronome is Off / Count-in only /
     On, with the click level of the selected bus (rack-wide).
   - The Track tab shows only the rows of the track's type; the type can't be changed.
   - Mixes are the rack's buses: renaming and outputs edit the rig, for every song.
   - Not there until step 4: adding plugins, plugin windows, meters, the Banks window, playing
     from the edit cursor (Ctrl-Space plays from the left locator), the click on/off per mix.
   - `--snapshot <file.png>` renders the window to a PNG and quits (layout checks).
4. Add the library features above and wire them: meters, locate, metronome per mix, plugin
   windows, then BanksEditor. Done:
   - Library: `playFrom` (renderer command; count-in only from the left locator), meters
     (`takeTrackPeak` / `takeMasterPeak` / `takeMetronomePeak`, written by the audio thread),
     `setMetronomeInMix` (rig.json `metronomeOn`), device channel counts and names,
     `copyPlaylist` (model and folder), `getRunningPlugins` / `effectChanged` /
     `getPluginHost` for plugin editors. TCP: `playFrom`, `setMetronomeInMix`. Unloading the
     playlist now emits `playlistLoaded` with an empty playlist. The plugin host registers the
     formats with editors. Tested in `Tests/MBoxCoreFeatureTests.cpp`, `MBoxRendererTests.cpp`.
   - UI: `PluginWindows` (editor windows; running instance, or an own instance for a bank slot
     no loaded song uses; closed and reopened when the library rebuilds the playlist), the
     plugin menu and scan, insert Add / open / bypass / remove, meters, the click On / Off per
     mix, play from the edit cursor (space, Ctrl-Space), the count-in countdown, playlist copy /
     paste, channel names in the routing menus, the Banks window (names, chains, plugins).
   - The plugin scan runs on the message thread: the app doesn't respond while it scans.
5. Tuner and loudness stay hidden until implemented.
