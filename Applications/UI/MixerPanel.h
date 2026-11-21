#pragma once

#include "PluginWindows.h"
#include "Session.h"
#include "UIHelpers.h"

/** Bottom mixer, showing the selected mix bus (Session::getSelectedBus()): one channel strip per
    track of the selected song (inserts, balance, fader, mute / solo), a Metronome strip while the
    song's metronome isn't off (its level in this bus, rack-wide), and the bus's master strip
    (master chain, balance, volume and the outputs the bus goes to, rack-wide). SubmixPanel, next
    to it, selects the bus.

    Faders, balance, mute / solo and the click's on / off work during playback (the library ramps
    to the new values), and so do plugin windows. Inserts are added and removed while stopped.
*/
class MixerPanel final : public ui::Panel,
                         private Session::Listener,
                         private juce::Timer
{
public:
    MixerPanel (Session& session, PluginWindows& pluginWindows);
    ~MixerPanel() override;

    void resized() override;

    /** A bus's outputs as shown: "N/A" also when the audio device doesn't have them. */
    static juce::String outputsText (Session& session, std::pair<int, int> outputs);

private:
    struct Strip;
    class InsertSlots;

    void sessionChanged() override;
    void sessionMixChanged() override  { sessionChanged(); }
    void timerCallback() override;
    void rebuildStrips();
    void layoutStrips();
    /** The click has a strip while the selected song's metronome isn't off. */
    bool showsClick (const MBoxSong& song) const;

    Session& m_session;
    PluginWindows& m_pluginWindows;
    juce::Viewport m_viewport;
    juce::Component m_content;
    std::vector<std::unique_ptr<Strip>> m_strips;
    std::vector<int> m_stripTrackIds;   // the track ids the strips were built for; 0 = metronome, -1 = master

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MixerPanel)
};
