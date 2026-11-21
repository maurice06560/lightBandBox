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

    /** The menu item id of "N/A" (MBoxMaster::noOutputs), only offered for a mix. */
    static constexpr int noOutputsId = 3;

    static MBoxTrack::Outputs outputsForId (int id);
    static int idForOutputs (MBoxTrack::Outputs);
    /** The same choices as a popup menu (item ids as outputsForId), the current one ticked. forMix
        offers "N/A" instead of "Default".
    */
    static juce::PopupMenu outputsMenu (const juce::StringArray& outputNames, MBoxTrack::Outputs current, bool forMix = false);
    /** Short form for tight spaces: "1/2", "3/4", "5 mono", "N/A". */
    static juce::String outputsText (MBoxTrack::Outputs);

private:
    void sessionChanged() override;
    void showInList (Session::ListTab, const Bar&) override;

    Session& m_session;
    ui::SegmentedControl m_tabs { { "Song", "Track", "Tempo", "Signature", "Bankslots" } };
    ui::Label m_lockedNote { "Stop (S) to edit. The metronome and track volumes stay live.", ui::Label::Style::Caption };
    std::vector<std::unique_ptr<juce::Component>> m_pages;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ListEditorPanel)
};
