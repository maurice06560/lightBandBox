# Auto mix

Design proposal, not implemented. Goal: a band presses **Auto mix**, plays 10 to 20 seconds of
a song, and gets a baseline mix in which every instrument can be heard. They then adjust it by
ear. It doesn't aim at a finished mix.

The flow:

1. The user selects a song, places the edit cursor on a representative section (everyone
   playing, a chorus for example) and presses **Auto mix**.
2. The song plays from the cursor for N seconds (10 to 20), then stops by itself. Meanwhile the
   live inputs are recorded.
3. A background analysis measures every track and computes a proposal: volumes, balances,
   inserts and master processing.
4. The user compares the proposal with the current mix (A/B), then applies or cancels it. An
   applied proposal can be undone.

The rest of this document covers each step, what it needs from the engine, and the decisions
still open.

## 1. Capture

### WAV tracks: nothing to record

WAVs are already decoded in memory (`MBoxAudioClipCache`). The analysis only needs the frame
range that was played, and reads the clips directly.

### Live inputs: recorded on the audio thread

- Tap: each physical track after its bank-slot chain (the instrument's sound, amp sim included),
  before inserts, fader and pan. That's in `MBoxRenderer::renderSegment`.
- Buffers allocated on the message thread when Auto mix starts: about 3.5 MB per mono track for
  20 s at 44.1 kHz in float. The audio thread writes into them and advances an atomic write
  index: no allocation, lock or logging (CLAUDE.md rule).
- Several tracks on the same input share one live input (`MBoxPlaylistPlayer::m_liveInputs`
  keyed by input and n-th track): record per input, then attribute to tracks.

### Input level check

While capturing, track each input's peak and loudness. An input that clips, or sits far below a
usable level, is a problem at the audio interface's preamp that the mix can't fix: report it
("Input 2 clips: lower its gain on the interface") instead of compensating.

## 2. Analysis

Runs on a background thread after the stop, on the captured live audio and the WAV ranges. For
each track:

| Measure | What for |
|---|---|
| Integrated and short-term loudness (LUFS, ITU-R BS.1770: K-weighting, gating) | volume |
| Peak and crest factor (peak vs RMS), loudness range | compression, headroom |
| Energy per third-octave band, spectral centroid, low-end energy | EQ, high-pass |
| Masking: per pair of tracks, the bands where both are strong | corrective EQ cuts |
| Stereo width and correlation (stereo WAVs) | balance, mono compatibility |

The LUFS measurement is shared with the bank-slot loudness meter of the postponed step 5
(`Docs/ImplementationPlan.md`): write it once, as a reusable class in `Sources/Audio`.

## 3. Track roles (model change)

Rules need to know what each track is, and names aren't reliable. Add a role to `MBoxTrack`,
saved in `track.json`, chosen in the Track page:

`kick`, `snare`, `drums` (overheads or a stereo drum stem), `bass`, `rhythmGuitar`,
`leadGuitar`, `keys`, `leadVocal`, `backingVocal`, `backingTrack`, `other`, `none` (excluded
from auto mix, e.g. click or cue tracks).

Each role has a priority (who wins when two tracks mask each other), a loudness target, a pan
rule and a high-pass frequency. These form a table that can be tuned, and later saved per band.

Later, instrument recognition could suggest roles; the user still confirms them.

## 4. Decisions (rule-based)

Rule-based automatic mixing is well documented (for example Perez-Gonzalez and Reiss) and
predictable on stage. No machine learning is needed for a baseline.

- **Volume**: set each track's loudness relative to a reference track (the lead vocal when
  present). Example targets, to tune: lead vocal 0 LU, snare and kick -2, bass -3, guitars -4,
  keys -5, backing vocals -6. Then shift everything so the bus sum leaves headroom (around
  -18 LUFS before the master, peaks below -6 dBFS).
- **Pan and balance**: by role. Kick, snare, bass and lead vocal centred. Two tracks of the same
  role (two rhythm guitars, two backing vocals) spread left and right, for example ±0.6. Keys
  slightly off-centre. Stereo WAVs keep balance 0 unless their correlation shows a problem.
- **EQ**:
  - a high-pass on every track except kick and bass, at the role's frequency (80 to 120 Hz);
  - where two tracks mask each other, a cut of 2 to 4 dB in that band on the lower-priority
    track (bass under kick around 60 to 100 Hz, guitars around the vocal presence at 2 to 4 kHz);
  - optionally, a cut on a strong narrow resonance found in a track's spectrum.
- **Compression**: only where the dynamics call for it (high loudness range: lead vocal, often
  bass). Threshold from the measured loudness distribution, ratio 2:1 to 4:1, moderate attack
  and release.
- **Master**: a gentle glue compressor (around 2:1, 1 to 2 dB of reduction) and a limiter with
  a -1 dBTP ceiling.

## 5. The effects

The algorithm sets values with meaning ("-3 dB at 250 Hz, Q 1.5"). The engine only sees a
plugin's parameters as normalised 0 to 1 values (`setEffectParameter`), mapped differently by
every AU and VST3. The rack is probably on Windows, so Apple's AU effects can't be the answer.

Options:

- **Built-in effects (recommended).** An EQ, a compressor and a limiter written with
  `juce::dsp`, hosted as a new effect format (`MBoxEffect::m_format = "Internal"`) in the same
  `MBoxEffectChain`s. Known parameters, the same on every platform, no licences, no lookahead.
  Saving, plugin windows and the TCP effect methods keep working.
- **Curated third-party plugins.** A table mapping each supported plugin's parameters, value
  curves and versions. Fragile: a plugin update can change the mapping.

### Latency

An insert with lookahead on a physical track delays what the musicians hear in their monitors.
And since every path is padded to the playlist's highest latency, a new latent chain forces a
full playlist rebuild (the single-song rebuild falls back to it). Auto mix uses zero-latency
processing only (minimum-phase EQ, compressors and limiters without lookahead), at least on
live inputs.

## 6. Applying the proposal

- Write it to the model, then notify the library:
  - track volume, balance and inserts in the song's `MBoxTrackMix`es, then `songChanged`: a
    single-song rebuild, which only creates the new chains, so it's fast;
  - the master chain in `MBoxMixBus`, then `rigChanged`: a full playlist rebuild.
- Which bus: only the one selected as the main or front-of-house mix. The others are the
  musicians' monitor mixes and stay as they are.
- Undo: keep a snapshot of the song's mixes for the bus (and of the master chain) before
  applying.

### Verifying before proposing

`MBoxRenderer` runs without an audio device (the tests use it offline). With the captured live
audio standing in for the inputs and the WAV clips, render the proposed mix offline, measure
each track's loudness in the result, and correct once before showing the proposal. This needs a
way to feed recorded audio as renderer inputs, which the renderer already takes as plain
buffers.

## 7. User interface and protocol

- UI: an **Auto mix** button (transport bar or mixer), a duration setting (10 or 20 s), a
  progress display while capturing and analysing, then a summary of the proposal per track
  (volume change, pan, inserts added), with **A/B**, **Apply**, **Cancel**, and later **Undo**.
- A/B without a rebuild: build the proposal's chains, then switch the active set live, the way
  bypass already works.
- TCP: an `autoMix` method (params: bus, seconds; the selected song, from the cursor), with
  progress events (`autoMixCapturing`, `autoMixAnalysing`, `autoMixProposal`) and `autoMixApply`
  / `autoMixCancel`, so the ESP32 can trigger it from a footswitch.

## 8. Open decisions

- **Master processing per song or rack-wide.** Master chains are rack-wide (`rig.json`). A
  per-song master needs a model change; the alternative is generic master processing that suits
  every song.
- **Built-in effects or curated third-party plugins** (section 5).
- **Which bus** auto mix targets, and whether it can also propose monitor mixes (for example
  "more of me" per musician).
- **Where the role targets live**: built in, per rack, or per band profile.
- **Capture of WAV-only songs**: with no live input, the analysis needs no playback at all and
  can run instantly on the clips.

## 9. Phases

1. Track roles, capture, LUFS analysis, volume and pan only. No effects yet, but most of the
   "we can hear everyone" benefit.
2. Built-in EQ and compressor: high-pass filters, masking cuts, dynamics on vocals and bass.
3. Master processing, offline verification, A/B and undo, the TCP methods.
4. Later: instrument recognition to suggest roles, or a model learned from the band's own mixes.
