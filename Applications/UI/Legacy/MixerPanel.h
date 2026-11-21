#pragma once

#include "Session.h"
#include "UIHelpers.h"

/** Bottom mixer, showing the selected mix (Session::getSelectedMix()): one channel strip per track
    (insert plugins, fader, dB ruler, live meter), a Metronome strip (only while the metronome is on;
    no inserts; an On / Off button for whether the click plays in this mix, its level shared) and the mix's master strip (inserts, volume and the outputs the mix
    goes to). SubmixPanel, next to it, selects which mix the strips show.
    Faders work during playback; the engine ramps to the new volume. Inserts are added and removed
    while stopped; their windows work during playback too.
*/
class MixerPanel final : public ui::Panel,
                         private Session::Listener,
                         private juce::Timer
{
public:
    explicit MixerPanel (Session& session);
    ~MixerPanel() override;

    void resized() override;

    /** A mix's outputs as shown: "N/A" also when the audio device doesn't have them. */
    static juce::String outputsText (Session& session, const MBoxMaster& master);

private:
    struct Strip;
    class InsertSlots;

    void sessionChanged() override;
    void timerCallback() override;
    void rebuildStrips();
    void layoutStrips();
    /** The click has a strip (in every mix) while the metronome is on. */
    bool showsClick (const MBoxSong& song) const;

    Session& m_session;
    juce::Viewport m_viewport;
    juce::Component m_content;
    std::vector<std::unique_ptr<Strip>> m_strips;
    std::vector<int> m_stripTrackIds;   // the track ids the strips were built for; 0 = metronome, -1 = master

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MixerPanel)
};
