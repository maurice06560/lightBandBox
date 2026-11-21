#pragma once

#include "Session.h"
#include "UIHelpers.h"

/** Right-hand editor, like Logic's list editors: a segmented control switches between the song's
    settings, the selected track's settings, and the Tempo, Signature and Bankslot lists.
*/
class ListEditorPanel final : public ui::Panel,
                              private Session::Listener
{
public:
    explicit ListEditorPanel (Session& session);
    ~ListEditorPanel() override;

    void showTab (Session::ListTab tab);
    void resized() override;

    /** Output channels, 0-based: { left, right } for stereo, { channel, -1 } for mono and
        { -1, -1 } for none (as MBoxMixBus::getOutputs).
    */
    using Outputs = std::pair<int, int>;
    static constexpr Outputs noOutputs { -1, -1 };
    /** The menu item id of "N/A" (no outputs). */
    static constexpr int noOutputsId = 1;

    static Outputs outputsForId (int id);
    static int idForOutputs (Outputs);
    /** The output choices of the device (its output channel names) as a popup menu (item ids as
        outputsForId), the current one ticked. noneText names the "no outputs" choice.
    */
    static juce::PopupMenu outputsMenu (const std::vector<std::string>& outputNames, Outputs current,
                                        const juce::String& noneText = "N/A (not played)");
    /** Short form for tight spaces: "1/2", "3/4", "5 mono", "N/A". */
    static juce::String outputsText (Outputs);

private:
    void sessionChanged() override;
    void showInList (Session::ListTab, const Bar&) override;

    Session& m_session;
    ui::SegmentedControl m_tabs { { "Song", "Track", "Tempo", "Signature", "Bankslots" } };
    ui::Label m_lockedNote { "Stop (S) to edit. The mix, the metronome and the song end stay live.", ui::Label::Style::Caption };
    std::vector<std::unique_ptr<juce::Component>> m_pages;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ListEditorPanel)
};
