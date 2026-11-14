#pragma once

#include <cstdint>
#include <vector>
#include <utility>

struct Signature
{
    int numerator { 4 };
    int denominator { 4 };

    bool isValid() const noexcept
    {
        return numerator >= 1 && numerator <= 32
            && (denominator == 1 || denominator == 2 || denominator == 4
                || denominator == 8 || denominator == 16 || denominator == 32);
    }
};

/** A musical position inside a song.

    bar and beat are 1-based. A beat is split into divisionsPerBeat divisions (1-based),
    and each division into ticksPerDivision ticks (0-based), which gives 960 ticks per beat.
*/
struct Bar
{
    static constexpr int divisionsPerBeat = 4;
    static constexpr int ticksPerDivision = 240;
    // This is always 960
    static constexpr int ticksPerBeat = divisionsPerBeat * ticksPerDivision;

    int bar { 1 };
    int beat { 1 };
    int division { 1 };
    int tick { 0 };

    // Positions compare by bar, then beat, then division, then tick
    auto operator<=> (const Bar&) const = default;
};
