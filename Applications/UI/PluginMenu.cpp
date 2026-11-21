#include "PluginMenu.h"

#include <Plugins/MBoxPluginHost.h>

namespace PluginMenu
{
    juce::String formatLabel (const juce::String& formatName)
    {
        return formatName == "AudioUnit" ? juce::String ("AU") : formatName;
    }

    void show (Session& session, juce::Component& target, std::function<void (const juce::PluginDescription&)> onChosen)
    {
        auto* host = session.getCore().getPluginHost();
        juce::Array<juce::PluginDescription> plugins;

        if (host != nullptr)
            for (const auto& description : host->getKnownPlugins())
                if (! description.isInstrument)
                    plugins.add (description);

        std::sort (plugins.begin(), plugins.end(), [] (const auto& a, const auto& b)
        {
            const auto byMaker = a.manufacturerName.compareIgnoreCase (b.manufacturerName);
            return byMaker != 0 ? byMaker < 0 : a.name.compareIgnoreCase (b.name) < 0;
        });

        // One submenu per manufacturer; ids are 1 + the index into plugins
        juce::PopupMenu menu;
        juce::PopupMenu manufacturerMenu;
        juce::String manufacturer;

        auto flush = [&]
        {
            if (manufacturerMenu.getNumItems() > 0)
                menu.addSubMenu (manufacturer.isEmpty() ? juce::String ("(Unknown)") : manufacturer, manufacturerMenu);

            manufacturerMenu = {};
        };

        for (int i = 0; i < plugins.size(); ++i)
        {
            const auto& description = plugins.getReference (i);

            if (description.manufacturerName != manufacturer)
            {
                flush();
                manufacturer = description.manufacturerName;
            }

            manufacturerMenu.addItem (i + 1, description.name + "  (" + formatLabel (description.pluginFormatName) + ")");
        }

        flush();

        constexpr int scanId = -1;

        if (plugins.isEmpty())
            menu.addItem (-2, "No plugins known yet: scan for them first", false, false);

        menu.addSeparator();
        menu.addItem (scanId, plugins.isEmpty() ? "Scan for plugins" : "Rescan plugins (finds newly installed ones)");

        menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&target),
                            [&session, plugins, chosen = std::move (onChosen), safe = juce::Component::SafePointer<juce::Component> (&target)] (int result)
                            {
                                if (safe == nullptr || result == 0)
                                    return;

                                if (result == scanId)
                                    scan (session);
                                else if (result > 0 && result <= plugins.size())
                                    chosen (plugins.getReference (result - 1));
                            });
    }

    void scan (Session& session)
    {
        session.setStatus ("Scanning plugins: this can take a few minutes, and the app won't respond meanwhile...");

        // After the status bar has painted
        juce::Timer::callAfterDelay (200, [&session]
        {
            juce::MouseCursor::showWaitCursor();
            const auto result = session.getCore().scanForPlugins();
            juce::MouseCursor::hideWaitCursor();

            auto* host = session.getCore().getPluginHost();
            const auto count = host != nullptr ? host->getKnownPlugins().size() : 0;

            if (result.wasOk())
                session.setStatus ("Plugin scan done: " + juce::String (count) + " plugin(s) found");
            else
                session.setStatus ("Plugin scan failed: " + juce::String (result.m_message), true);
        });
    }
}
