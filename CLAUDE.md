# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project overview

lightOneBox3 ("MBox") is a C++20 audio engine built on JUCE, meant to ship as a **dynamic library** for a headless rack (audio interface + computer, controlled by an ESP32 over TCP) and, later, a JUCE UI app. Applications create a single `MBoxCoreAudio` instance (`Sources/MBoxCoreAudio.h`) and talk to audio only through its public API.

The top level is not a git repository. `JUCE/` is a vendored JUCE checkout (version 9.0.5) with its own git history. Treat it as read-only third-party code.

## Build and test

```sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug   # configure (first run takes ~30 s)
cmake --build build                                     # → build/libMBoxCore.dylib and the test runner
build/MBoxCoreTests_artefacts/Debug/MBoxCoreTests       # all tests; or pass a test name, e.g. MBoxRenderer
(cd build && ctest)                                     # the same, through CTest
```

`CMakeLists.txt` has three library targets, because JUCE has no `juce_add_*` helper for dynamic libraries:
- `MBoxCoreSources` (INTERFACE): every source, the JUCE modules and the `JUCE_*` config flags. Whatever links it compiles all of it, so one binary never holds two copies of JUCE.
- `MBoxCore` (SHARED): for the headless rack app. JUCE is compiled in, and symbols are hidden: only `MBOX_EXPORT` classes are exported (the macro comes from the generated `build/mbox_export.h`). A few bundled C libraries (WebP, Opus, FLAC) still export their own symbols.
- `MBoxCoreEmbedded` (INTERFACE, defines `MBOX_STATIC_DEFINE`): for the JUCE UI app and the tests, which compile the library into themselves next to their own JUCE modules.

Points to know:
- When adding a `.cpp`, list it in `MBoxCoreSources`' `target_sources` (and tests in `MBoxCoreTests`'). New JUCE modules and `JUCE_*` flags also go on `MBoxCoreSources`.
- Keep JUCE types out of `MBoxCoreAudio.h`: it uses `std::` types only, and forward-declares the model classes that only embedded apps use. Its implementation is a private `Impl`.
- Windows needs `-DMBOX_ASIO_SDK_DIR=<path>` for ASIO (the SDK isn't redistributable). macOS links CoreAudioKit for AU hosting.
- Tests are JUCE `UnitTest`s in the "MBox" category (`Tests/`), built with `JUCE_MODAL_LOOPS_PERMITTED=1` so they can run the message loop. They need no audio device (`MBoxCoreOptions::m_openAudioDevice = false` runs a "null device" thread instead); one test hosts Apple's AUDelay on macOS.

## Architecture

Threads: the **message thread** (the thread that created `MBoxCoreAudio`) runs the API, storage, TCP request handling and events; a **loader thread** builds playlist players; the **audio thread** only renders. Nothing crosses into the audio thread except through a lock-free FIFO (transport commands), atomics (mix values) or an atomic pointer swap (a whole new playlist player). See the comment at the top of `MBoxRenderer.h`.

- `Sources/MBoxCoreAudio.{h,cpp}`: the exported entry point. Its `Impl` owns, in order: `ScopedJuceInitialiser_GUI` (must outlive everything), the single-instance lock, `MBoxStorage`, the model (playlists, `MBoxRig`, banks), `MBoxPluginHost`, `MBoxAudioClipCache`, `MBoxAudioEngine`, `MBoxServer` and the loader pool. A 50 ms timer reports status as events and saves 1 s after the last change.
- `Sources/Model/`: plain data, edited on the message thread. `MBoxTypes.h` (constants, `Bar`, `Signature`, enums), `MBoxTimeline` (Bar ↔ ticks ↔ seconds ↔ frames), `MBoxSong` (grids, locators, tracks, per-bus `MBoxTrackMix`), `MBoxTrack` (+ audio, physical, info), `MBoxPlaylist`, `MBoxRig` (`MBoxMixBus`, device settings), `MBoxBank` (`MBoxEffect`, slots).
- `Sources/Persistence/MBoxStorage`: the JSON data folder (layout in its header), written through temporary files.
- `Sources/Audio/`: `MBoxAudioEngine` (device, null device, device API), `MBoxRenderer` (transport, fades, mixing, routing; device-independent), `MBoxSongPlayer` (runtime players and `buildPlaylistPlayer`), `MBoxAudioClipCache` (WAVs decoded to 16-bit), `MBoxClickSynth`.
- `Sources/Plugins/`: `MBoxPluginHost` (plugin list, creation on the message thread), `MBoxEffectChain` (channel adaptation, latency padding, warm-up), `MBoxLiveInput` (slot chains with crossfaded switching and tails).
- `Sources/TCPServer/`: `MBoxComAPI.h` (the newline-delimited JSON protocol: methods, events, error codes) and `MBoxServer`.

### Design references

- `Docs/DataModel.md`: the data model copied from the Lucidchart diagram, every design decision taken since, and where the code differs from the diagram.
- `Docs/ImplementationPlan.md`: the phased plan and its status.
- The design document (`OneBox-Full-designV1.docx`, kept outside the repo) defines the timing model: 960 PPQ, positions as `{bar, beat-unit, division, tick}`, and an authoritative `int64` sample-frame transport.

### Decided constraints

- The library must build on Windows (the likely rack OS), macOS and Linux. It targets C++20.
- Only 44100 Hz, 16-bit stereo WAV files are supported for now.
- A bank slot is a chain of AU or VST3 plugins. AU only exists on macOS.
- Only one `MBoxCoreAudio` instance (and one process using it) may run at a time.
- `Main.cpp` and the host applications are not part of this library.
- The JUCE UI app will link MBoxCore as a static library, so only one copy of JUCE is in its process. The headless rack app links the shared library.

## Conventions

- Members use the `m_` prefix, and default values go in brace initializers (`double m_sampleRate { 44100.0 };`).
- Section banners are `//====...` comments placed above each function or section.
- Log with `juce::Logger::writeToLog`, prefixing each message with the class name (`"MBoxAudioEngine: ..."`).
- Classes end with `JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR`. Free helper functions go in an anonymous namespace at the top of the `.cpp` file.
- Nothing on the audio thread (`audioDeviceIOCallbackWithContext`, `MBoxRenderer::process` and everything it calls) may allocate, lock or log.
- Comment every method in headers whose behaviour isn't obvious from its name: threads, units, ranges, failure cases.

## Looking up the JUCE API (from JUCE/README.md)

Use the vendored source as the reference, not online docs or memory, because the APIs may have changed:
- Each module's main header, `JUCE/modules/juce_<module>/juce_<module>.h`, lists its public headers and dependencies.
- To find a class, grep its declaration and read the Doxygen comments, for example `grep -rlE "^\s*(class|struct)\s+(JUCE_API\s+)?AudioDeviceManager\b" JUCE/modules`.
- Check `JUCE/BREAKING_CHANGES.md` before relying on JUCE knowledge from older versions. The current version is set by the macros in `JUCE/modules/juce_core/system/juce_StandardHeader.h`.
