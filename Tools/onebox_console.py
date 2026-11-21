#!/usr/bin/env python3
"""A console remote for the OneBox rack: connects to its TCP server and offers a menu to list
playlists and songs, select a song, play, pause, stop, play from a position, set the metronome
and look at the mix.

The protocol is described in Sources/TCPServer/MBoxComAPI.h: newline-delimited JSON, a "hello"
first, then requests answered by id, events pushed by the rack, and a "ping" at least every
second (the rack drops clients silent for 5 s).

Usage: python3 Tools/onebox_console.py [--host HOST] [--port PORT]
Standard library only; runs on Windows, macOS and Linux.
"""

import argparse
import json
import queue
import socket
import sys
import threading
import time

PROTOCOL_VERSION = 1
DEFAULT_PORT = 5050
PING_INTERVAL_S = 0.5
RESPONSE_TIMEOUT_S = 10.0


class RackError(Exception):
    """An error response from the rack: {"code": ..., "message": ...}."""

    def __init__(self, error):
        super().__init__(f"{error.get('message', 'error')} (code {error.get('code')})")


class RackConnection:
    """One TCP connection: sends requests, matches responses by id, keeps the latest transport
    state, prints the other events, and pings in the background."""

    def __init__(self, host, port):
        self.sock = socket.create_connection((host, port), timeout=5)
        self.sock.settimeout(None)
        self.send_lock = threading.Lock()
        self.pending = {}            # request id -> queue receiving its response
        self.pending_lock = threading.Lock()
        self.next_id = 1
        self.transport = {}          # the latest transport event's data
        self.closed = threading.Event()

        threading.Thread(target=self._read_loop, daemon=True).start()
        self.request("hello", {"protocolVersion": PROTOCOL_VERSION, "client": "onebox_console"})
        threading.Thread(target=self._ping_loop, daemon=True).start()

    def request(self, method, params=None):
        """Sends a request and waits for its result. Raises RackError on an error response."""
        if self.closed.is_set():
            raise RackError({"message": "not connected to the rack", "code": None})

        with self.pending_lock:
            request_id = self.next_id
            self.next_id += 1
            answer = queue.Queue(maxsize=1)
            self.pending[request_id] = answer

        message = {"id": request_id, "method": method}
        if params is not None:
            message["params"] = params

        self._send(message)

        try:
            response = answer.get(timeout=RESPONSE_TIMEOUT_S)
        except queue.Empty:
            raise RackError({"message": f"no answer to {method}", "code": None})
        finally:
            with self.pending_lock:
                self.pending.pop(request_id, None)

        if "error" in response:
            raise RackError(response["error"])

        return response.get("result", {})

    def close(self):
        self.closed.set()
        try:
            self.sock.close()
        except OSError:
            pass

    def _send(self, message):
        data = (json.dumps(message) + "\n").encode("utf-8")
        with self.send_lock:
            self.sock.sendall(data)

    def _ping_loop(self):
        while not self.closed.wait(PING_INTERVAL_S):
            try:
                self._send({"id": 0, "method": "ping"})
            except OSError:
                break

    def _read_loop(self):
        buffer = b""
        try:
            while not self.closed.is_set():
                chunk = self.sock.recv(65536)
                if not chunk:
                    break
                buffer += chunk
                while b"\n" in buffer:
                    line, buffer = buffer.split(b"\n", 1)
                    if line.strip():
                        self._handle(json.loads(line.decode("utf-8")))
        except (OSError, ValueError):
            pass

        if not self.closed.is_set():
            print("\n[disconnected from the rack]")
            self.closed.set()

        # Requests still waiting fail now rather than at their timeout
        with self.pending_lock:
            for answer in self.pending.values():
                answer.put({"error": {"message": "disconnected from the rack", "code": None}})

    def _handle(self, message):
        if "event" in message:
            self._handle_event(message["event"], message.get("data", {}))
            return

        with self.pending_lock:
            answer = self.pending.get(message.get("id"))

        if answer is not None:
            answer.put(message)

    def _handle_event(self, name, data):
        # Transport events come up to 20 times a second: kept, not printed
        if name == "transport":
            self.transport = data
        elif name == "playlistLoading":
            pass  # the progress is in the transport state
        elif name == "playlistLoaded":
            if data.get("ok"):
                print(f"\n[loaded {data.get('playlist')}]")
            else:
                print(f"\n[loading {data.get('playlist')} failed: {data.get('error')}]")
        elif name == "songChanged":
            print(f"\n[song: {data.get('playlist')} / {data.get('song')}]")
        elif name == "error":
            print(f"\n[rack error: {data.get('message')}]")


#==============================================================================
# Menu actions
#==============================================================================
def show_state(rack):
    state = rack.request("getState")
    line = (f"{state.get('state', '?')}  {state.get('playlist', '')} / {state.get('song', '')}  "
            f"at {state.get('position', '')}  {state.get('bpm', '')} BPM  "
            f"{state.get('numerator', '')}/{state.get('denominator', '')}")
    if state.get("loading"):
        line += f"  (loading {round(100 * state.get('loadingProgress', 0))}%)"
    print(line)


def list_playlists(rack):
    playlists = rack.request("getPlaylists").get("playlists", [])
    for index, name in enumerate(playlists, 1):
        print(f"  {index}. {name}")
    return playlists


def list_songs(rack, playlist=None):
    playlist = playlist or choose_playlist(rack)
    if playlist is None:
        return None, []
    songs = rack.request("getSongs", {"playlist": playlist}).get("songs", [])
    print(f"{playlist}:")
    for index, name in enumerate(songs, 1):
        print(f"  {index}. {name}")
    return playlist, songs


def choose_from(items, prompt):
    """Asks for a number (or a name) among items; None when cancelled."""
    answer = input(f"{prompt} (number or name, empty to cancel): ").strip()
    if not answer:
        return None
    if answer.isdigit() and 1 <= int(answer) <= len(items):
        return items[int(answer) - 1]
    for item in items:
        if item.lower() == answer.lower():
            return item
    print("Not in the list")
    return None


def choose_playlist(rack):
    playlists = list_playlists(rack)
    return choose_from(playlists, "Playlist") if playlists else None


def select_song(rack):
    playlist, songs = list_songs(rack)
    song = choose_from(songs, "Song") if songs else None
    if song is None:
        return

    rack.request("selectSong", {"playlist": playlist, "song": song})

    # Another playlist loads in the background: wait for it, showing the progress
    while not rack.closed.is_set():
        state = rack.request("getState")
        if not state.get("loading"):
            break
        print(f"\r  loading {playlist}... {round(100 * state.get('loadingProgress', 0))}%", end="", flush=True)
        time.sleep(0.2)
    print()
    show_state(rack)


def transport(method, params=None):
    """A transport command, then the state once the rack has applied it (it reports every 50 ms)."""
    def action(rack):
        rack.request(method, params)
        time.sleep(0.15)
        show_state(rack)
    return action


def play_from(rack):
    position = input("Position (bar.beat.division.tick, e.g. 9.1.1.0): ").strip()
    if position:
        transport ("playFrom", {"position": position}) (rack)


def set_metronome(rack):
    states = ["off", "fourCountdownOnly", "on"]
    for index, name in enumerate(states, 1):
        print(f"  {index}. {name}")
    state = choose_from(states, "Metronome")
    if state:
        rack.request("setMetronomeState", {"state": state})


def show_mix(rack):
    bus = ask_int("Mix bus (1-3)", 1)
    tracks = rack.request("getMix", {"bus": bus}).get("tracks", [])
    if not tracks:
        print("No tracks (no song selected?)")
    for track in tracks:
        flags = (" M" if track.get("mute") else "") + (" S" if track.get("solo") else "")
        print(f"  {track.get('trackId'):>3}  {track.get('name', ''):<24} {track.get('type', ''):<9} "
              f"{track.get('volume', 0):6.1f} dB  bal {track.get('balance', 0):+.2f}{flags}")


def set_track_volume(rack):
    bus = ask_int("Mix bus (1-3)", 1)
    track = ask_int("Track id", None)
    if track is None:
        return
    volume = ask_float("Volume (dB)", None)
    if volume is not None:
        rack.request("setTrackVolume", {"bus": bus, "track": track, "volume": volume})


def toggle_track(rack, method, field):
    bus = ask_int("Mix bus (1-3)", 1)
    track = ask_int("Track id", None)
    if track is None:
        return
    tracks = rack.request("getMix", {"bus": bus}).get("tracks", [])
    current = next((t for t in tracks if t.get("trackId") == track), None)
    if current is None:
        print("No such track")
        return
    rack.request(method, {"bus": bus, "track": track, field: not current.get(field)})
    print(f"{field} {'on' if not current.get(field) else 'off'}")


def ask_int(prompt, default):
    answer = input(f"{prompt}{f' [{default}]' if default is not None else ''}: ").strip()
    if not answer:
        return default
    try:
        return int(answer)
    except ValueError:
        print("Not a number")
        return None


def ask_float(prompt, default):
    answer = input(f"{prompt}: ").strip()
    if not answer:
        return default
    try:
        return float(answer)
    except ValueError:
        print("Not a number")
        return None


MENU = [
    ("s", "State", show_state),
    ("l", "List playlists", list_playlists),
    ("g", "List the songs of a playlist", lambda rack: list_songs(rack)),
    ("o", "Select a song", select_song),
    ("p", "Play (resumes after a pause)", transport ("play")),
    ("a", "Pause", transport ("pause")),
    ("t", "Stop", transport ("stop")),
    ("f", "Play from a position", play_from),
    ("m", "Metronome of the selected song", set_metronome),
    ("x", "Show a mix bus", show_mix),
    ("v", "Set a track's volume", set_track_volume),
    ("u", "Mute / unmute a track", lambda rack: toggle_track(rack, "setTrackMute", "mute")),
    ("z", "Solo / unsolo a track", lambda rack: toggle_track(rack, "setTrackSolo", "solo")),
]


def print_menu():
    print()
    for key, label, _ in MENU:
        print(f"  {key}  {label}")
    print("  q  Quit")


def main():
    parser = argparse.ArgumentParser(description="Console remote for the OneBox rack")
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--port", type=int, default=DEFAULT_PORT)
    args = parser.parse_args()

    try:
        rack = RackConnection(args.host, args.port)
    except (OSError, RackError) as error:
        print(f"Can't connect to {args.host}:{args.port}: {error}")
        return 1

    print(f"Connected to the rack at {args.host}:{args.port}")
    actions = {key: action for key, _, action in MENU}

    try:
        while not rack.closed.is_set():
            print_menu()
            choice = input("> ").strip().lower()

            if choice == "q":
                break

            action = actions.get(choice)
            if action is None:
                if choice:
                    print("Unknown choice")
                continue

            try:
                action(rack)
            except RackError as error:
                print(f"Error: {error}")
    except (EOFError, KeyboardInterrupt):
        print()
    finally:
        rack.close()

    return 0


if __name__ == "__main__":
    sys.exit(main())
