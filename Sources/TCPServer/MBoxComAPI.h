#pragma once

//==============================================================================
// The TCP protocol between the rack and its clients (the ESP32 controller, later a Python
// AI layer). It is newline-delimited JSON: every message is one JSON object on one line,
// UTF-8, ending with '\n', at most maxMessageBytes long.
//
// Requests (client -> rack), in the style of JSON-RPC:
//   {"id": 12, "method": "setTrackVolume", "params": {"bus": 1, "track": 3, "volume": -6.0}}
// Responses (rack -> client), with the request's id:
//   {"id": 12, "result": {...}}
//   {"id": 12, "error": {"code": 4, "message": "No track 3"}}
// Events (rack -> every client, unrequested):
//   {"event": "transport", "data": {...}}
//
// Session rules:
// - The first request must be "hello" with the client's protocolVersion. Any other request
//   before it fails with ErrorCode::helloRequired.
// - Clients send "ping" at least every second. A client silent for clientTimeoutMs is
//   disconnected, so a dropped controller is noticed quickly.
// - Several clients can be connected at once; events go to all of them.
// - Numbers are JSON numbers (never strings), so no locale issue. Bus, bank, slot and track
//   numbers are 1-based. Positions are "bar.beat.division.tick" strings.
//==============================================================================
namespace MBoxComAPI
{
    constexpr int protocolVersion = 1;
    constexpr int defaultPort = 5050;
    constexpr int maxMessageBytes = 65536;
    constexpr int clientTimeoutMs = 5000;

    //==========================================================================
    // Methods. Params and results are listed as name: type.
    namespace Method
    {
        // params: protocolVersion: int, client: string. result: protocolVersion: int, server: string
        constexpr const char* hello = "hello";
        // no params, empty result
        constexpr const char* ping = "ping";
        // result: the transport event's data
        constexpr const char* getState = "getState";
        // result: playlists: [string]
        constexpr const char* getPlaylists = "getPlaylists";
        // result: playlists: [string] (same as getPlaylists, named after MBoxCoreAudio::getPlaylistNames)
        constexpr const char* getPlaylistNames = "getPlaylistNames";
        // params: playlist. result: songs: [string]
        constexpr const char* getSongs = "getSongs";
        // params: playlist, newName. Fails (rejected) if newName is invalid or already used
        constexpr const char* renamePlaylist = "renamePlaylist";
        // params: playlist, song, newName. Fails (rejected) if newName is invalid or already used
        constexpr const char* renameSong = "renameSong";
        // params: playlist, song. Loading continues with playlistLoading/playlistLoaded events
        constexpr const char* selectSong = "selectSong";
        constexpr const char* play = "play";
        constexpr const char* pause = "pause";
        constexpr const char* stop = "stop";
        // params: position ("bar.beat.division.tick"). Plays the selected song from there
        constexpr const char* playFrom = "playFrom";
        // params: bus. result: tracks: [{trackId, name, type, volume, balance, mute, solo}]
        constexpr const char* getMix = "getMix";
        // params: bus, track, and volume (dB) / balance (-1..1) / mute (bool) / solo (bool)
        constexpr const char* setTrackVolume = "setTrackVolume";
        constexpr const char* setTrackBalance = "setTrackBalance";
        constexpr const char* setTrackMute = "setTrackMute";
        constexpr const char* setTrackSolo = "setTrackSolo";
        // params: bus, and volume (dB) / balance (-1..1)
        constexpr const char* setBusVolume = "setBusVolume";
        constexpr const char* setBusBalance = "setBusBalance";
        // params: bus, volume (dB)
        constexpr const char* setMetronomeVolume = "setMetronomeVolume";
        // params: state ("off", "fourCountdownOnly", "on"), for the selected song
        constexpr const char* setMetronomeState = "setMetronomeState";
        // params: bus, on (bool). Whether the click plays in that mix at all (rack-wide)
        constexpr const char* setMetronomeInMix = "setMetronomeInMix";
        // params: target. result: effects: [{name, manufacturer, bypassed}]
        // target: {"slot": {"bank": b, "slot": s}}, {"insert": {"bus": b, "track": t}} or {"master": {"bus": b}}
        constexpr const char* getEffects = "getEffects";
        // params: target, effect (0-based index). result: parameters: [{id, name, value}]
        constexpr const char* getEffectParameters = "getEffectParameters";
        // params: target, effect, parameterId: string, value: 0..1
        constexpr const char* setEffectParameter = "setEffectParameter";
        // params: target, effect, bypassed: bool
        constexpr const char* setEffectBypass = "setEffectBypass";
    }

    //==========================================================================
    // Events
    namespace Event
    {
        // state, playlist, song, position, bpm, numerator, denominator, loading, loadingProgress.
        // Sent on every change of state, song or position (at most 20 times per second).
        constexpr const char* transport = "transport";
        // playlist, progress (0..1)
        constexpr const char* playlistLoading = "playlistLoading";
        // playlist, ok: bool, error: string. An empty playlist: nothing is loaded any more
        // (the loaded playlist, or its last song, was removed)
        constexpr const char* playlistLoaded = "playlistLoaded";
        // playlist, song
        constexpr const char* songChanged = "songChanged";
        // bus, and the changed track or bus values
        constexpr const char* mixChanged = "mixChanged";
        // the device's type, input, output, bufferSize, sampleRate
        constexpr const char* deviceChanged = "deviceChanged";
        // message
        constexpr const char* error = "error";
    }

    //==========================================================================
    enum ErrorCode
    {
        parseError         = 1,  // not a JSON object, or a line too long
        unknownMethod      = 2,
        badParameters      = 3,  // a parameter is missing or has the wrong type
        rejected           = 4,  // the request was valid but refused (see the message): unknown name, wrong state...
        helloRequired      = 5,
        unsupportedVersion = 6,
        internalError      = 7
    };
}
