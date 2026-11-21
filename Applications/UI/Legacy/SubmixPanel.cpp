#include "SubmixPanel.h"
#include "MixerPanel.h"

void SubmixPanel::MixButton::mouseDown (const juce::MouseEvent& e)
{
    if (e.mods.isPopupMenu())
    {
        juce::PopupMenu menu;
        menu.addItem ("Rename...", [safe = juce::Component::SafePointer<MixButton> (this)]
                      {
                          if (safe != nullptr && safe->onRename != nullptr)
                              safe->onRename();
                      });
        menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this));
        return;
    }

    ui::Button::mouseDown (e);
}

void SubmixPanel::MixButton::mouseDoubleClick (const juce::MouseEvent&)
{
    if (onRename != nullptr)
        onRename();
}

//==============================================================================
SubmixPanel::SubmixPanel (Session& session)
    : ui::Panel ("Submixes"), m_session (session)
{
    for (int m = 0; m < MBoxSong::numMixes; ++m)
    {
        auto& button = m_buttons[(size_t) m];
        button.onClick = [this, m] { m_session.selectMix (m); };
        button.onRename = [this, m] { rename (m); };
        addAndMakeVisible (button);
    }

    m_session.addListener (this);
    sessionChanged();
}

SubmixPanel::~SubmixPanel()
{
    m_session.removeListener (this);
}

void SubmixPanel::resized()
{
    // Stacked from the top, in line with the mixer's first row.
    auto area = getContentBounds().reduced (6, 0).withTrimmedTop (2);

    for (auto& button : m_buttons)
    {
        button.setBounds (area.removeFromTop (30));
        area.removeFromTop (12);
    }
}

void SubmixPanel::sessionChanged()
{
    auto* song = m_session.getSelectedSong();

    for (int m = 0; m < MBoxSong::numMixes; ++m)
    {
        auto& button = m_buttons[(size_t) m];
        button.setVisible (song != nullptr);

        if (song == nullptr)
            continue;

        const auto& mix = song->getMix (m);
        button.setButtonText (mix.getName());
        button.setToggleState (m == m_session.getSelectedMixIndex(), juce::dontSendNotification);
        button.setTooltip (mix.getName() + " goes to " + MixerPanel::outputsText (m_session, mix.getMaster())
                           + ". Click to show it in the mixer; double-click or right-click to rename it.");
    }
}

void SubmixPanel::rename (int mix)
{
    auto* song = m_session.getSelectedSong();

    if (song == nullptr)
        return;

    const auto current = song->getMix (mix).getName();
    UIHelpers::askForText ("Rename mix", "New name for " + current + ":", current, "Rename",
                           [this, mix] (const juce::String& name) { m_session.renameMix (mix, name); });
}
