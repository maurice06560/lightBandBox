#pragma once

#include "Session.h"

#include <juce_audio_processors/juce_audio_processors.h>

/** The plugin editor windows of the app (inserts, master chains and bank slots).

    A window shows the running plugin when its playlist is loaded (what's heard changes at once):
    its edits are reported to the library (MBoxCoreAudio::effectChanged), which saves them and
    copies them to the plugin's other running instances. A bank slot that no loaded song uses
    has no running plugin: it opens in an instance of its own, whose state is written into the
    banks (and saved) as it changes.

    When the library replaces its running playlist (any edit rebuilds it), the windows showing
    running plugins close before those plugins are freed, and open again on the new ones.
    Message thread only.
*/
class PluginWindows final : private Session::Listener
{
public:
    explicit PluginWindows (Session& session);
    ~PluginWindows() override;

    /** Opens (or brings to front) the editor of an effect. Reports in the status bar when it
        can't: an insert or master effect whose song isn't loaded yet, a plugin that can't be
        created, or one without an editor.
    */
    void open (const MBoxEffectTarget& target, int effectIndex, const juce::String& title);

    /** Like open(), for an effect just added: it only runs once the library has rebuilt the
        playlist, so it opens then (an effect of a bank slot no song uses opens at once).
    */
    void openWhenRunning (const MBoxEffectTarget& target, int effectIndex, const juce::String& title);

    /** Closes the windows of a target's effects (one effect, or all with effectIndex -1), e.g.
        before removing or moving them.
    */
    void close (const MBoxEffectTarget& target, int effectIndex = -1);
    void closeAll();

    static MBoxEffectTarget slotTarget (MBoxSlotRef slot);
    static MBoxEffectTarget insertTarget (int bus, int trackId);
    static MBoxEffectTarget masterTarget (int bus);

private:
    class Window;

    void sessionChanged() override {}
    void sessionMixChanged() override {}
    void sessionPlayerReplaced() override;

    /** The effect a target refers to in the model, or nullptr. */
    MBoxEffect* findEffect (const MBoxEffectTarget& target, int effectIndex);
    void closeWindow (Window* window);

    Session& m_session;
    std::vector<std::unique_ptr<Window>> m_windows;

    struct Pending
    {
        MBoxEffectTarget target;
        int effectIndex;
        juce::String title;
    };

    std::vector<Pending> m_pending;
    bool m_reopening = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PluginWindows)
};
