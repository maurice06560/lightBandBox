#pragma once

#include "Session.h"
#include "UIHelpers.h"

/** "Submixes", beside the mixer: one button per mix of the selected song, stacked. Clicking one
    shows that mix in the mixer (it lights blue); double-click or right-click renames it.
*/
class SubmixPanel final : public ui::Panel,
                          private Session::Listener
{
public:
    static constexpr int preferredWidth = 128;

    explicit SubmixPanel (Session& session);
    ~SubmixPanel() override;

    void resized() override;

private:
    /** Lit blue while its mix is selected; double-click or right-click renames it. */
    struct MixButton final : public ui::Button
    {
        MixButton() : ui::Button ({}, ui::Button::Variant::Mute, true) { setClickingTogglesState (false); }

        void mouseDown (const juce::MouseEvent& e) override;
        void mouseDoubleClick (const juce::MouseEvent&) override;

        std::function<void()> onRename;
    };

    void sessionChanged() override;

    void sessionMixChanged() override {}
    void rename (int mix);

    Session& m_session;
    std::array<MixButton, MBoxSong::numMixes> m_buttons;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SubmixPanel)
};
