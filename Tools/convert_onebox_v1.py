#!/usr/bin/env python3
"""Converts a data folder of the previous OneBox application (MBoxApplication) to the format of
the MBox library (MBoxStorage, schemaVersion 1).

    python3 Tools/convert_onebox_v1.py ~/OneBox ~/OneBox2

The source folder is only read: the converted data and copies of the WAV files are written to
the destination, which must not exist or be empty. A report lists everything that couldn't be
converted exactly.

Old format -> new format:
- Banks and slots are 1-based in both. Banks get a 5th, empty slot (the old ones had 4).
  Plugin effects keep their identity and state; built-in effects (Chorus...) and the measured
  loudness are dropped.
- startBar -> left locator. The old END was after the last bar: the song gets one more bar
  and the right locator goes there, so the same bars play.
- Mixes were per song with their own master (volume, balance, outputs); the library's mix buses
  are rack-wide. Each bus takes the most common master settings and name. A song whose master
  differed has the difference added to its track volumes in that mix (a silent or unrouted
  master makes them silent), so it sounds the same.
- Balance -64..+63 -> -1..+1. A null volume (silent) -> -100 dB.
- Metronome: click on -> On; countdown "only" -> count-in only; otherwise Off. The click level
  becomes rack-wide per bus (the most common level of the songs that click in it; buses no song
  clicks in are silent). The tone is dropped.
- Physical inputs were 1-based (0 meaning the first one); they become 0-based. Output channels
  were 1-based; they become 0-based.
- Input monitoring is dropped.
"""

import argparse
import collections
import json
import shutil
import sys
import wave
import xml.etree.ElementTree as ElementTree
from pathlib import Path

SCHEMA_VERSION = 1
NUMBER_OF_MIX_BUS = 3
NUMBER_OF_BANKS = 5
NUMBER_OF_SLOTS = 5
MIN_VOLUME_DB = -100.0
MAX_VOLUME_DB = 6.0


class Report:
    def __init__(self):
        self.lines = []

    def add(self, message):
        self.lines.append(message)

    def print(self):
        print("\nConversion notes (%d):" % len(self.lines))

        for line in self.lines:
            print("  - " + line)


def read_json(path):
    with open(path, "r", encoding="utf-8") as stream:
        return json.load(stream)


def write_json(path, data):
    path.parent.mkdir(parents=True, exist_ok=True)

    with open(path, "w", encoding="utf-8") as stream:
        json.dump(data, stream, indent=2)
        stream.write("\n")


def position_text(position):
    return "%d.%d.%d.%d" % (position["bar"], position["beat"], position["division"], position["tick"])


def volume(value):
    """Old volumes: dB, or null for silent."""
    if value is None:
        return MIN_VOLUME_DB

    return max(MIN_VOLUME_DB, min(MAX_VOLUME_DB, float(value)))


def balance(value):
    """Old balances: -64 (left) .. +63 (right)."""
    value = int(value or 0)
    return max(-1.0, min(1.0, value / 64.0 if value < 0 else value / 63.0))


def outputs(old):
    """Old outputs: 1-based channels, (left, right), (channel, -1) mono, (-1, -1) none."""
    first, second = old

    if first is None or first < 1:
        return [-1, -1]

    return [first - 1, second - 1 if second is not None and second >= 1 else -1]


def most_common(values):
    return collections.Counter(values).most_common(1)[0][0]


def convert_effect(effect, where, report):
    if effect.get("type") != "Plugin":
        report.add("%s: built-in effect %s dropped (the library only hosts AU / VST3 plugins)" % (where, effect.get("type")))
        return None

    plugin = ElementTree.fromstring(effect["plugin"])
    unique_id = int(plugin.get("uniqueId", "0"), 16)

    if unique_id >= 1 << 31:
        unique_id -= 1 << 32   # PluginDescription::uniqueId is a signed int

    return {
        "name": plugin.get("name", effect.get("name", "")),
        "format": plugin.get("format", ""),
        "manufacturer": plugin.get("manufacturer", ""),
        "identifier": plugin.get("file", ""),
        "uniqueId": unique_id,
        "version": plugin.get("version", ""),
        "state": effect.get("state") or "0.",
        "bypassed": False,
    }


def convert_banks(source, report):
    old = read_json(source / "banks.json")["banks"]
    banks = []

    for b in range(NUMBER_OF_BANKS):
        old_bank = old[b] if b < len(old) else {"name": "Bank %d" % (b + 1), "bankSlots": []}
        slots = []

        for s in range(NUMBER_OF_SLOTS):
            old_slot = old_bank["bankSlots"][s] if s < len(old_bank["bankSlots"]) else {"name": "Slot %d" % (s + 1), "effectChain": []}
            where = "Bank %s / %s" % (old_bank["name"], old_slot["name"])
            effects = [e for e in (convert_effect (e, where, report) for e in old_slot.get("effectChain", [])) if e is not None]

            if old_slot.get("loudness") is not None:
                report.add("%s: measured loudness %.1f LUFS dropped (not in the library yet)" % (where, old_slot["loudness"]))

            slots.append({"name": old_slot["name"], "effects": effects})

        banks.append({"name": old_bank["name"], "slots": slots})

    return {"schemaVersion": SCHEMA_VERSION, "banks": banks}


def load_songs(source):
    """[(playlist, song name, song json, tracks json)] in playlist order."""
    playlists = read_json(source / "playlists" / "playlists.json")["playlists"]
    songs = []

    for playlist in playlists:
        for name in playlist["songs"]:
            folder = source / "playlists" / playlist["name"] / "songs" / name
            songs.append((playlist["name"], name, read_json(folder / "song.json"), read_json(folder / "tracks" / "track.json")))

    return playlists, songs


def decide_rig(songs, report):
    """The rack-wide mix buses: the most common master settings, name and click level."""
    buses = []

    for bus in range(NUMBER_OF_MIX_BUS):
        mixes = [song["mixes"][bus] for _, _, song, _ in songs if bus < len(song["mixes"])]
        masters = [m["master"] for m in mixes]

        names = [m["name"] for m in mixes]
        custom = [n for n in names if n != "Mix %d" % (bus + 1)]
        name = most_common(custom) if custom else "Mix %d" % (bus + 1)

        if len(set(names)) > 1:
            report.add("Mix %d was named %s depending on the playlist: it is now \"%s\" for every song"
                       % (bus + 1, " / ".join(sorted(set(names))), name))

        routed = [tuple(outputs(m["outputs"])) for m in masters if outputs(m["outputs"])[0] >= 0]
        bus_outputs = list(most_common(routed)) if routed else [-1, -1]
        master_volume = most_common([volume(m["volume"]) for m in masters]) if masters else 0.0
        master_balance = most_common([balance(m["balance"]) for m in masters]) if masters else 0.0

        # The click level: the most common one among the songs that click in this bus
        levels = [volume(song["metronome"]["volume"]) for _, _, song, _ in songs
                  if (song.get("metronomeActive") or song["metronome"].get("countdown") == "only")
                  and (bus + 1) in song["metronome"].get("mixes", [])]
        click = most_common(levels) if levels else MIN_VOLUME_DB

        buses.append({"id": bus + 1, "name": name, "outputs": bus_outputs, "masterVolume": master_volume,
                      "masterBalance": master_balance, "metronomeVolume": click, "masterEffects": []})

    return {"schemaVersion": SCHEMA_VERSION, "device": {"type": "", "input": "", "output": "", "bufferSize": 0}, "mixBuses": buses}


def convert_song(playlist, name, song, rig, report):
    where = "%s / %s" % (playlist, name)
    total_bars = int(song["totalBars"])

    # Metronome
    metronome = song["metronome"]
    countdown = metronome.get("countdown", "off")
    active = bool(song.get("metronomeActive"))
    state = "on" if active and countdown != "only" else "fourCountdownOnly" if countdown == "only" else "off"

    if active and countdown == "off":
        report.add("%s: the click had no count-in; the library's \"on\" always counts in" % where)

    if not active and countdown == "on":
        report.add("%s: countdown \"on\" with the click off: no count-in now (metronome off)" % where)

    if state != "off":
        for bus in metronome.get("mixes", []):
            if rig["mixBuses"][bus - 1]["metronomeVolume"] <= MIN_VOLUME_DB:
                report.add("%s: the click played in %s, which no other song clicks in: silent there for now "
                           "(the click on/off per mix comes later)" % (where, rig["mixBuses"][bus - 1]["name"]))
            elif abs(volume(metronome["volume"]) - rig["mixBuses"][bus - 1]["metronomeVolume"]) > 0.05:
                report.add("%s: click level %.1f dB is now %.1f dB in %s (rack-wide)"
                           % (where, volume(metronome["volume"]), rig["mixBuses"][bus - 1]["metronomeVolume"], rig["mixBuses"][bus - 1]["name"]))

    # Mixes: the per-song master is folded into the track volumes
    mixes = []

    for bus, mix in enumerate(song["mixes"][:NUMBER_OF_MIX_BUS]):
        rig_bus = rig["mixBuses"][bus]
        master = mix["master"]
        silent = master["volume"] is None or outputs(master["outputs"])[0] < 0
        offset = volume(master["volume"]) - rig_bus["masterVolume"]

        if silent:
            report.add("%s: %s's master was silent or unrouted: its tracks are silent in that mix" % (where, rig_bus["name"]))
        elif abs(offset) > 0.05:
            report.add("%s: %s's master was %+.1f dB from the rack's: added to its track volumes" % (where, rig_bus["name"], offset))

        if not silent and outputs(master["outputs"]) != rig_bus["outputs"]:
            report.add("%s: %s went to other outputs (%s); it now uses the rack's (%s)"
                       % (where, rig_bus["name"], master["outputs"], [c + 1 for c in rig_bus["outputs"]]))

        if abs(balance(master["balance"]) - rig_bus["masterBalance"]) > 0.02:
            report.add("%s: %s's master balance %s dropped (rack-wide now)" % (where, rig_bus["name"], master["balance"]))

        tracks = []

        for entry in mix["tracks"]:
            level = MIN_VOLUME_DB if silent or entry["volume"] is None else volume(volume(entry["volume"]) + offset)

            if not silent and entry["volume"] is not None and volume(entry["volume"]) + offset > MAX_VOLUME_DB:
                report.add("%s: track %d in %s capped at +6 dB" % (where, entry["track"], rig_bus["name"]))

            tracks.append({"trackId": entry["track"], "volume": level, "balance": balance(entry["balance"]),
                           "mute": bool(entry["mute"]), "solo": bool(entry["solo"]), "inserts": []})

            if entry.get("inserts"):
                report.add("%s: track %d inserts in %s dropped (none expected)" % (where, entry["track"], rig_bus["name"]))

        mixes.append({"bus": bus + 1, "tracks": tracks})

    at_end = song.get("atEnd", {"action": "stop", "delay": 0})

    return {
        "schemaVersion": SCHEMA_VERSION,
        "name": name,
        "totalBars": total_bars + 1,
        "leftLocator": "%d.1.1.0" % int(song.get("startBar", 1)),
        "rightLocator": "%d.1.1.0" % (total_bars + 1),
        "endSongAction": "continue" if at_end.get("action") == "next" else "stop",
        "endSongDelay": int(at_end.get("delay", 0)),
        "metronome": state,
        "tempos": [{"position": position_text(t["position"]), "bpm": t["bpm"]} for t in song["tempoGrid"]],
        "signatures": [{"position": position_text(s["position"]), "numerator": s["numerator"], "denominator": s["denominator"]}
                       for s in song["signatureGrid"]],
        "mixes": mixes,
    }


def convert_tracks(where, old_tracks, source_tracks, destination_tracks, report):
    tracks = []

    for track in old_tracks:
        entry = {"id": track["id"], "name": track["name"]}

        if track["trackType"] == "Physical":
            entry.update({"type": "physical", "physicalInput": max(1, int(track.get("physicalInput") or 0)) - 1,
                          "diOutput": -1,
                          "bankSlots": [{"position": position_text(b["position"]), "bank": b["bank"], "slot": b["slot"]}
                                        for b in track.get("bankSlots", [])]})

            if track.get("diSend"):
                di = outputs(track["diSend"])
                entry["diOutput"] = di[0]

                if di[1] >= 0:
                    report.add("%s, %s: the stereo DI send became mono (output %d)" % (where, track["name"], di[0] + 1))

            if track.get("monitor"):
                report.add("%s, %s: input monitoring while stopped dropped" % (where, track["name"]))

        elif track["trackType"] == "Wav":
            entry["type"] = "audio"
            file = track.get("file")

            if file:
                entry["audioStart"] = position_text(file["position"])
                source = source_tracks / file["path"]
                frames = 0

                if source.exists():
                    with wave.open(str(source), "rb") as reader:
                        frames = reader.getnframes()

                        if (reader.getframerate(), reader.getsampwidth(), reader.getnchannels()) != (44100, 2, 2):
                            report.add("%s, %s: %s is %d Hz, %d-bit, %d channel(s): the library plays 44100 Hz 16-bit stereo only"
                                       % (where, track["name"], file["path"], reader.getframerate(), reader.getsampwidth() * 8,
                                          reader.getnchannels()))
                else:
                    report.add("%s, %s: %s is missing" % (where, track["name"], file["path"]))

                entry["file"] = file["path"]
                entry["lengthInFrames"] = frames
        else:
            report.add("%s, %s: unknown track type %s skipped" % (where, track["name"], track["trackType"]))
            continue

        tracks.append(entry)

    # Every file of the media folder, used or not, as the old app kept them
    source_media = source_tracks / "media"

    if source_media.is_dir():
        for file in sorted(source_media.iterdir()):
            if file.is_file() and not file.name.startswith("."):
                (destination_tracks / "media").mkdir(parents=True, exist_ok=True)
                shutil.copy2(file, destination_tracks / "media" / file.name)

    return {"schemaVersion": SCHEMA_VERSION, "tracks": tracks}


def main():
    parser = argparse.ArgumentParser(description="Convert a previous-version OneBox data folder to the MBox library format.")
    parser.add_argument("source", type=Path, help="the old data folder (only read)")
    parser.add_argument("destination", type=Path, help="the new data folder (must not exist, or be empty)")
    arguments = parser.parse_args()

    source = arguments.source.expanduser().resolve()
    destination = arguments.destination.expanduser().resolve()

    if not (source / "playlists" / "playlists.json").is_file() or not (source / "banks.json").is_file():
        sys.exit("%s is not an old OneBox data folder (playlists/playlists.json and banks.json expected)" % source)

    if destination == source or source in destination.parents or destination in source.parents:
        sys.exit("The destination must be outside the source folder, and not contain it")

    if destination.exists() and any(destination.iterdir()):
        sys.exit("%s isn't empty: choose a new folder" % destination)

    report = Report()
    playlists, songs = load_songs(source)
    rig = decide_rig(songs, report)

    destination.mkdir(parents=True, exist_ok=True)
    write_json(destination / "banks.json", convert_banks(source, report))
    write_json(destination / "rig.json", rig)
    write_json(destination / "playlists" / "playlists.json",
               {"schemaVersion": SCHEMA_VERSION, "playlists": [{"name": p["name"], "songs": list(p["songs"])} for p in playlists]})

    for playlist, name, song, tracks in songs:
        where = "%s / %s" % (playlist, name)
        source_folder = source / "playlists" / playlist / "songs" / name
        destination_folder = destination / "playlists" / playlist / "songs" / name
        write_json(destination_folder / "song.json", convert_song(playlist, name, song, rig, report))
        write_json(destination_folder / "tracks" / "track.json",
                   convert_tracks(where, tracks, source_folder / "tracks", destination_folder / "tracks", report))
        print("Converted %s" % where)

    report.print()
    print("\nDone: %s" % destination)


if __name__ == "__main__":
    main()
