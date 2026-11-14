#pragma once

/** Base class of all the tracks of a song. */
class MBoxTrack
{
public:
    virtual ~MBoxTrack() = default;

protected:
    MBoxTrack() = default;

    int m_id { 0 };
};
