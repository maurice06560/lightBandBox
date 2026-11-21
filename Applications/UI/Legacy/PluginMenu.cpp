#include "PluginMenu.h"
#include "../Effects/PluginHost.h"

namespace PluginMenu
{
    juce::String formatLabel (const juce::PluginDescription& description)
    {
        return description.pluginFormatName == "AudioUnit" ? juce::String ("AU") : description.pluginFormatName;
    }

    void show (Session& session, juce::Component& target, std::function<void (const juce::PluginDescription&)> onChosen)
    {
        auto& host = PluginHost::get();
        const auto plugins = host.getEffectPlugins();

        // One submenu per manufacturer; ids are 1 + the index into plugins.
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

            manufacturerMenu.addItem (i + 1, description.name + "  (" + formatLabel (description) + ")");
        }

        flush();

        constexpr int scanId = -1;

        if (plugins.isEmpty())
            menu.addItem (-2, host.isScanning() ? "Scanning..." : "No plugins known yet: scan for them first", false, false);

        menu.addSeparator();
        menu.addItem (scanId, plugins.isEmpty() ? "Scan for plugins" : "Rescan plugins (finds newly installed ones)", ! host.isScanning());

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
        session.setStatus ("Scanning plugins (each one is loaded in a separate process)...");

        PluginHost::get().startScan ([&session] (const juce::String& plugin, float progress)
                                     {
                                         session.setStatus ("Scanning plugins (" + juce::String (juce::roundToInt (progress * 100.0f)) + "%): " + plugin);
                                     },
                                     [&session] (const juce::String& summary)
                                     {
                                         session.setStatus ("Plugin scan done: " + summary);
                                     });
    }

    std::unique_ptr<MAudioEffect> create (Session& session, const juce::PluginDescription& description)
    {
        juce::MouseCursor::showWaitCursor();
        auto plugin = std::make_unique<MAudioEffect> (description);
        juce::MouseCursor::hideWaitCursor();

        if (plugin->isMissing())
        {
            session.setStatus ("Cannot load " + description.name + ": " + plugin->getLoadError(), true);
            return nullptr;
        }

        return plugin;
    }
}
