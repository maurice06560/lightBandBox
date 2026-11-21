#include "MBoxTypes.h"

#include <cstdio>

//==============================================================================
// Bar text conversion
//==============================================================================
std::string Bar::toString() const
{
    return std::to_string (m_bar) + "." + std::to_string (m_beat) + "."
         + std::to_string (m_division) + "." + std::to_string (m_tick);
}

bool Bar::fromString (const std::string& text, Bar& result)
{
    Bar parsed;
    int consumed = 0;

    if (std::sscanf (text.c_str(), "%d.%d.%d.%d%n", &parsed.m_bar, &parsed.m_beat,
                     &parsed.m_division, &parsed.m_tick, &consumed) != 4
        || consumed != (int) text.size())
        return false;

    result = parsed;
    return true;
}

//==============================================================================
// Enum names
//==============================================================================
const char* toString (EndSongAction action)
{
    return action == EndSongAction::Continue ? "continue" : "stop";
}

const char* toString (MetronomeState state)
{
    switch (state)
    {
        case MetronomeState::FourCountdownOnly: return "fourCountdownOnly";
        case MetronomeState::On:                return "on";
        case MetronomeState::Off:               break;
    }

    return "off";
}

const char* toString (TransportState state)
{
    switch (state)
    {
        case TransportState::Playing: return "playing";
        case TransportState::Paused:  return "paused";
        case TransportState::Stopped: break;
    }

    return "stopped";
}

bool fromString (const std::string& text, EndSongAction& result)
{
    if (text == "stop")      { result = EndSongAction::Stop;     return true; }
    if (text == "continue")  { result = EndSongAction::Continue; return true; }
    return false;
}

bool fromString (const std::string& text, MetronomeState& result)
{
    if (text == "off")               { result = MetronomeState::Off;               return true; }
    if (text == "fourCountdownOnly") { result = MetronomeState::FourCountdownOnly; return true; }
    if (text == "on")                { result = MetronomeState::On;                return true; }
    return false;
}
