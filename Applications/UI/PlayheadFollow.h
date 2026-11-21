#pragma once

#include <algorithm>
#include <cmath>
#include <limits>

/** Keeps a playing playhead in the middle of the arrangement, frame by frame.

    Each frame, step() gets the playhead's distance from the middle (pixels, positive to the right)
    and returns how far to scroll the view right:
    - out of the visible timeline (more than half its width away, either side), all of it: the
      playhead is put back in the middle at once, and held there after;
    - left of the middle, nothing: the playhead walks up to the middle, and is held there after;
    - right of the middle, the excess shrinks by a fraction per frame, so the playhead glides back
      (about a second from the far edge at 30 frames per second), then is held.
    The excess is measured from the previous frame's gap, not the current one, so the song moving on
    adds no lag however fast it scrolls.
*/
class PlayheadFollow
{
public:
    static constexpr float catchUpPerFrame = 0.15f;

    /** While stopped or paused: where the playhead will start from (stopped: the edit cursor).
        Playback then continues from there: held if it is at (or reaches) the middle, gliding back
        if it is right of it.
    */
    void park (float gap) noexcept { m_gap = std::max (0.0f, gap); }

    /** The user scrolled or zoomed while playing: glide back from wherever the playhead is now. */
    void reset() noexcept { m_gap = std::numeric_limits<float>::infinity(); }

    float step (float gap, float halfWidth) noexcept
    {
        if (std::abs (gap) > halfWidth)
        {
            m_gap = 0.0f;
            return gap;
        }

        if (gap <= 0.0f)
        {
            m_gap = 0.0f;
            return 0.0f;
        }

        auto next = std::min (gap, m_gap) * (1.0f - catchUpPerFrame);

        if (next < 1.0f)
            next = 0.0f;

        m_gap = next;
        return gap - next;
    }

private:
    float m_gap = std::numeric_limits<float>::infinity();
};
