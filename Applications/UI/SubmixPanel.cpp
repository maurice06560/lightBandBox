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
    for (int bus = 1; bus <= numberOfMixBus; ++bus)
    {
        auto& button = m_buttons[(size_t) bus - 1];
        button.onClick = [this, bus] { m_session.selectBus (bus); };
        button.onRename = [this, bus] { rename (bus); };
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
    // Stacked from the top, in line with the mixer's first row
    auto area = getContentBounds().reduced (6, 0).withTrimmedTop (2);

    for (auto& button : m_buttons)
    {
        button.setBounds (area.removeFromTop (30));
        area.removeFromTop (12);
    }
}

void SubmixPanel::sessionChanged()
{
    for (int bus = 1; bus <= numberOfMixBus; ++bus)
    {
        auto& button = m_buttons[(size_t) bus - 1];
        const auto& mixBus = m_session.getRig().m_mixBuses[(size_t) bus - 1];

        button.setButtonText (mixBus.getName());
        button.setToggleState (bus == m_session.getSelectedBus(), juce::dontSendNotification);
        button.setTooltip (mixBus.getName() + " goes to " + MixerPanel::outputsText (m_session, mixBus.getOutputs())
                           + ". Click to show it in the mixer; double-click or right-click to rename it.");
    }
}

void SubmixPanel::rename (int bus)
{
    const auto current = m_session.getRig().m_mixBuses[(size_t) bus - 1].getName();

    UIHelpers::askForText ("Rename mix", "New name for " + current + ":", current, "Rename",
                           [this, bus] (const juce::String& name)
                           {
                               m_session.editRig ([bus, name] (MBoxRig& rig)
                                                  {
                                                      return rig.m_mixBuses[(size_t) bus - 1].rename (name)
                                                                 ? juce::Result::ok() : juce::Result::fail ("Invalid mix name");
                                                  },
                                                  "Mix renamed to " + name);
                           });
}
