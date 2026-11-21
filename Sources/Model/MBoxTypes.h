#pragma once

#include <cstdint>
#include <string>
#include <tuple>

//==============================================================================
// Global limits and musical constants shared by the model and the engine
//==============================================================================
constexpr int numberOfMixBus          = 3;
constexpr int numberOfBanks           = 5;
constexpr int numberOfSlotsPerBanks   = 5;
constexpr int maxNumberOfTrackPerSong = 16;

constexpr int defaultTotalBar        = 32;
constexpr int defaultTempo           = 120;
constexpr int minTempo               = 20;
constexpr int maxTempo               = 400;
constexpr int maxEndSongDelaySeconds = 30;

constexpr int ticksPerQuarterNote = 960;
constexpr int ticksPerDivision    = 240;  // one division is a sixteenth note
constexpr int countInBeats        = 4;

// The only sample rate supported for WAV files and for the engine
constexpr double supportedSampleRate = 44100.0;

constexpr double minVolumeDb = -100.0;  // at or below this, a volume is treated as silent (-inf)
constexpr double maxVolumeDb = 6.0;

// Length of the fades applied on play, pause, stop and at the locators
constexpr double transportFadeSeconds = 0.010;

//==============================================================================
// A musical position {bar, beat-unit, division, tick}.
// Bars, beat-units and divisions start at 1; ticks start at 0 and go up to 239.
// A beat-unit is one denominator unit of the time signature (an eighth note in 6/8).
//==============================================================================
struct Bar
{
    int m_bar      { 1 };
    int m_beat     { 1 };
    int m_division { 1 };
    int m_tick     { 0 };

    // True when the position is the first tick of a bar
    bool isStartOfBar() const noexcept  { return m_beat == 1 && m_division == 1 && m_tick == 0; }

    // Formats the position as "bar.beat.division.tick", for example "12.3.1.0"
    std::string toString() const;

    // Parses "bar.beat.division.tick". Returns false, leaving result untouched, when the text is malformed
    static bool fromString (const std::string& text, Bar& result);

    auto asTuple() const noexcept  { return std::tie (m_bar, m_beat, m_division, m_tick); }
    bool operator== (const Bar& other) const noexcept  { return asTuple() == other.asTuple(); }
    bool operator!= (const Bar& other) const noexcept  { return asTuple() != other.asTuple(); }
    bool operator<  (const Bar& other) const noexcept  { return asTuple() <  other.asTuple(); }
    bool operator<= (const Bar& other) const noexcept  { return asTuple() <= other.asTuple(); }
    bool operator>  (const Bar& other) const noexcept  { return asTuple() >  other.asTuple(); }
    bool operator>= (const Bar& other) const noexcept  { return asTuple() >= other.asTuple(); }
};

//==============================================================================
// A time signature. Only the denominators 2, 4, 8 and 16 are supported.
//==============================================================================
struct Signature
{
    int m_numerator   { 4 };
    int m_denominator { 4 };

    bool isValid() const noexcept
    {
        return m_numerator >= 1 && m_numerator <= 32
            && (m_denominator == 2 || m_denominator == 4 || m_denominator == 8 || m_denominator == 16);
    }

    int ticksPerBeatUnit() const noexcept      { return ticksPerQuarterNote * 4 / m_denominator; }
    int ticksPerBar() const noexcept           { return m_numerator * ticksPerBeatUnit(); }
    int divisionsPerBeatUnit() const noexcept  { return ticksPerBeatUnit() / ticksPerDivision; }

    bool operator== (const Signature& other) const noexcept
    {
        return m_numerator == other.m_numerator && m_denominator == other.m_denominator;
    }
    bool operator!= (const Signature& other) const noexcept  { return ! (*this == other); }
};

//==============================================================================
// What happens when the playhead reaches a song's right locator
//==============================================================================
enum class EndSongAction
{
    Stop,
    Continue
};

//==============================================================================
// When the metronome clicks during a song
//==============================================================================
enum class MetronomeState
{
    Off,                // no clicks and no count-in
    FourCountdownOnly,  // a 4-beat-unit count-in before the left locator, then silence
    On                  // the count-in, then a click on every beat-unit until the right locator
};

//==============================================================================
// Transport states reported by the engine
//==============================================================================
enum class TransportState
{
    Stopped,
    Playing,
    Paused
};

// Converts the enums to and from the names used in the JSON files and the TCP protocol
const char* toString (EndSongAction action);
const char* toString (MetronomeState state);
const char* toString (TransportState state);
bool fromString (const std::string& text, EndSongAction& result);
bool fromString (const std::string& text, MetronomeState& result);
