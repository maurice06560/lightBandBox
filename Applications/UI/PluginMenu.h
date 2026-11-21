#pragma once

#include "Session.h"

#include <juce_audio_processors/juce_audio_processors.h>

/** The installed-plugin picker shared by the Banks window and the mixer's insert slots. The list
    comes from the library's plugin host (plugins.xml in the data folder), filled by a scan.
*/
namespace PluginMenu
{
    /** "AU" or "VST3". */
    juce::String formatLabel (const juce::String& formatName);

    /** Shows the known effect plugins in one submenu per manufacturer, plus a (re)scan item, under
        target. onChosen is called with the plugin picked (not if target was deleted meanwhile).
    */
    void show (Session& session, juce::Component& target, std::function<void (const juce::PluginDescription&)> onChosen);

    /** Scans the installed plugins and saves the list. Slow (seconds to minutes) and the app
        doesn't respond meanwhile: the status bar says so first.
    */
    void scan (Session& session);
}
