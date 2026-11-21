#pragma once

#include "Session.h"
#include "../Effects/MAudioEffect.h"

/** The installed-plugin picker shared by the Banks window and the mixer's insert slots. */
namespace PluginMenu
{
    /** "AU" or "VST3". */
    juce::String formatLabel (const juce::PluginDescription& description);

    /** Shows the known effect plugins in one submenu per manufacturer, plus a (re)scan item, under
        target. onChosen is called with the plugin picked (not if target was deleted meanwhile).
    */
    void show (Session& session, juce::Component& target, std::function<void (const juce::PluginDescription&)> onChosen);

    /** Scans for plugins in the background, reporting progress in the status bar. */
    void scan (Session& session);

    /** A new, independent instance of the plugin (with a wait cursor: loading can take a second).
        Reports the problem and returns nullptr when it can't be loaded.
    */
    std::unique_ptr<MAudioEffect> create (Session& session, const juce::PluginDescription& description);
}
