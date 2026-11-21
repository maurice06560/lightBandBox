#include "PluginWindows.h"

#include <Plugins/MBoxPluginHost.h>

namespace
{
    bool sameTarget (const MBoxEffectTarget& a, const MBoxEffectTarget& b)
    {
        if (a.m_kind != b.m_kind)
            return false;

        switch (a.m_kind)
        {
            case MBoxEffectTarget::Kind::Slot:   return a.m_bank == b.m_bank && a.m_slot == b.m_slot;
            case MBoxEffectTarget::Kind::Insert: return a.m_bus == b.m_bus && a.m_track == b.m_track;
            case MBoxEffectTarget::Kind::Master: return a.m_bus == b.m_bus;
        }

        return false;
    }
}

//==============================================================================
/** One editor window. Listens to its plugin and reports changes a few times a second at most. */
class PluginWindows::Window final : public juce::DocumentWindow,
                                    private juce::AudioProcessorListener,
                                    private juce::Timer
{
public:
    Window (PluginWindows& owner, const MBoxEffectTarget& target, int effectIndex, const juce::String& title,
            juce::AudioPluginInstance& plugin, std::unique_ptr<juce::AudioPluginInstance> ownInstance)
        : juce::DocumentWindow (title, juce::Colours::black, juce::DocumentWindow::closeButton | juce::DocumentWindow::minimiseButton),
          m_owner (owner), m_target (target), m_effectIndex (effectIndex), m_plugin (plugin), m_ownInstance (std::move (ownInstance))
    {
        setUsingNativeTitleBar (true);
        setContentOwned (m_plugin.createEditorAndMakeActive(), true);
        setResizable (getContentComponent() != nullptr && dynamic_cast<juce::AudioProcessorEditor*> (getContentComponent())->isResizable(), false);
        m_plugin.addListener (this);
        centreWithSize (juce::jmax (200, getWidth()), juce::jmax (100, getHeight()));
        setVisible (true);
        startTimer (300);
    }

    ~Window() override
    {
        stopTimer();
        m_plugin.removeListener (this);
        reportIfChanged();
        clearContentComponent();   // the editor goes before its plugin
    }

    bool hasEditor() const noexcept                { return getContentComponent() != nullptr; }
    bool isRunningPlugin() const noexcept          { return m_ownInstance == nullptr; }
    const MBoxEffectTarget& getTarget() const      { return m_target; }
    int getEffectIndex() const noexcept            { return m_effectIndex; }

    void closeButtonPressed() override
    {
        // Deletes this window: nothing after it
        m_owner.closeWindow (this);
    }

private:
    // Any thread: just note it
    void audioProcessorParameterChanged (juce::AudioProcessor*, int, float) override  { m_changed = true; }
    void audioProcessorChanged (juce::AudioProcessor*, const ChangeDetails& details) override
    {
        if (details.parameterInfoChanged || details.programChanged || details.nonParameterStateChanged)
            m_changed = true;
    }

    void timerCallback() override  { reportIfChanged(); }

    void reportIfChanged()
    {
        if (! m_changed.exchange (false))
            return;

        // Not running: the model gets the state directly
        if (! isRunningPlugin())
            if (auto* effect = m_owner.findEffect (m_target, m_effectIndex))
                m_plugin.getStateInformation (effect->m_state);

        m_owner.m_session.getCore().effectChanged (m_target, m_effectIndex);
    }

    PluginWindows& m_owner;
    const MBoxEffectTarget m_target;
    const int m_effectIndex;
    juce::AudioPluginInstance& m_plugin;
    std::unique_ptr<juce::AudioPluginInstance> m_ownInstance;   // null when showing a running plugin
    std::atomic<bool> m_changed { false };
};

//==============================================================================
PluginWindows::PluginWindows (Session& session) : m_session (session)
{
    m_session.addListener (this);
}

PluginWindows::~PluginWindows()
{
    m_session.removeListener (this);
    closeAll();
}

MBoxEffectTarget PluginWindows::slotTarget (MBoxSlotRef slot)
{
    MBoxEffectTarget target;
    target.m_kind = MBoxEffectTarget::Kind::Slot;
    target.m_bank = slot.m_bank;
    target.m_slot = slot.m_slot;
    return target;
}

MBoxEffectTarget PluginWindows::insertTarget (int bus, int trackId)
{
    MBoxEffectTarget target;
    target.m_kind = MBoxEffectTarget::Kind::Insert;
    target.m_bus = bus;
    target.m_track = trackId;
    return target;
}

MBoxEffectTarget PluginWindows::masterTarget (int bus)
{
    MBoxEffectTarget target;
    target.m_kind = MBoxEffectTarget::Kind::Master;
    target.m_bus = bus;
    return target;
}

MBoxEffect* PluginWindows::findEffect (const MBoxEffectTarget& target, int effectIndex)
{
    std::vector<MBoxEffect>* chain = nullptr;

    switch (target.m_kind)
    {
        case MBoxEffectTarget::Kind::Slot:
        {
            const MBoxSlotRef slot { target.m_bank, target.m_slot };

            if (slot.isValid())
                chain = &m_session.getBanks()[(size_t) slot.m_bank - 1].m_bankSlots[(size_t) slot.m_slot - 1].m_effectChain;

            break;
        }

        case MBoxEffectTarget::Kind::Insert:
            if (auto* song = m_session.getSelectedSong())
                if (auto* trackMix = song->getTrackMix (target.m_bus, target.m_track))
                    chain = &trackMix->m_inserts;
            break;

        case MBoxEffectTarget::Kind::Master:
            if (target.m_bus >= 1 && target.m_bus <= numberOfMixBus)
                chain = &m_session.getRig().m_mixBuses[(size_t) target.m_bus - 1].getMasterEffectChain();
            break;
    }

    return chain != nullptr && juce::isPositiveAndBelow (effectIndex, (int) chain->size()) ? &(*chain)[(size_t) effectIndex] : nullptr;
}

void PluginWindows::open (const MBoxEffectTarget& target, int effectIndex, const juce::String& title)
{
    for (auto& window : m_windows)
    {
        if (sameTarget (window->getTarget(), target) && window->getEffectIndex() == effectIndex)
        {
            window->toFront (true);
            return;
        }
    }

    auto* effect = findEffect (target, effectIndex);

    if (effect == nullptr)
    {
        m_session.setStatus ("That effect no longer exists", true);
        return;
    }

    std::unique_ptr<juce::AudioPluginInstance> ownInstance;
    juce::AudioPluginInstance* plugin = nullptr;

    if (const auto running = m_session.getCore().getRunningPlugins (target, effectIndex); ! running.empty())
    {
        plugin = running.front();
    }
    else if (target.m_kind == MBoxEffectTarget::Kind::Slot)
    {
        // Not used by the loaded playlist: an instance of its own, from the saved state
        auto* host = m_session.getCore().getPluginHost();
        juce::String error;
        juce::MouseCursor::showWaitCursor();

        if (host != nullptr)
            ownInstance = host->createInstance (*effect, supportedSampleRate, 512, error);

        juce::MouseCursor::hideWaitCursor();

        if (ownInstance == nullptr)
        {
            if (! m_reopening)
                m_session.setStatus ("Can't open " + effect->m_name + ": " + (error.isNotEmpty() ? error : juce::String ("no plugin host")), true);

            return;
        }

        plugin = ownInstance.get();
    }
    else
    {
        if (! m_reopening)
            m_session.setStatus ("Wait for " + m_session.getSelectedSongName() + " to finish loading to open " + effect->m_name, true);

        return;
    }

    if (! plugin->hasEditor())
    {
        m_session.setStatus (effect->m_name + " has no window of its own", true);
        return;
    }

    auto window = std::make_unique<Window> (*this, target, effectIndex, title, *plugin, std::move (ownInstance));

    if (! window->hasEditor())
    {
        m_session.setStatus ("Can't open the window of " + effect->m_name, true);
        return;
    }

    m_windows.push_back (std::move (window));
}

void PluginWindows::openWhenRunning (const MBoxEffectTarget& target, int effectIndex, const juce::String& title)
{
    if (! m_session.getCore().getRunningPlugins (target, effectIndex).empty() || target.m_kind == MBoxEffectTarget::Kind::Slot)
        open (target, effectIndex, title);
    else
        m_pending.push_back ({ target, effectIndex, title });
}

void PluginWindows::closeWindow (Window* window)
{
    // Deferred: the window may be the one calling (its close button)
    juce::MessageManager::callAsync ([this, window, alive = juce::WeakReference<juce::Component> (window)]
    {
        if (alive == nullptr)
            return;

        m_windows.erase (std::remove_if (m_windows.begin(), m_windows.end(), [window] (const auto& w) { return w.get() == window; }),
                         m_windows.end());
    });
}

void PluginWindows::close (const MBoxEffectTarget& target, int effectIndex)
{
    m_windows.erase (std::remove_if (m_windows.begin(), m_windows.end(),
                                     [&] (const auto& w)
                                     {
                                         return sameTarget (w->getTarget(), target) && (effectIndex < 0 || w->getEffectIndex() == effectIndex);
                                     }),
                     m_windows.end());
}

void PluginWindows::closeAll()
{
    m_windows.clear();
}

void PluginWindows::sessionPlayerReplaced()
{
    // The running plugins are about to be freed: close their windows, then open them again on
    // the new ones (where the effect still exists and runs). Windows with their own instance stay,
    // unless their slot now runs (one instance to edit, the one heard).
    struct Reopen
    {
        MBoxEffectTarget target;
        int effectIndex;
        juce::String title;
        juce::Rectangle<int> bounds;
    };

    std::vector<Reopen> reopen;

    for (auto it = m_windows.begin(); it != m_windows.end();)
    {
        if ((*it)->isRunningPlugin() || ! m_session.getCore().getRunningPlugins ((*it)->getTarget(), (*it)->getEffectIndex()).empty())
        {
            reopen.push_back ({ (*it)->getTarget(), (*it)->getEffectIndex(), (*it)->getName(), (*it)->getBounds() });
            it = m_windows.erase (it);
        }
        else
        {
            ++it;
        }
    }

    const juce::ScopedValueSetter<bool> quiet (m_reopening, true);

    for (const auto& entry : reopen)
    {
        if (m_session.getCore().getRunningPlugins (entry.target, entry.effectIndex).empty())
            continue;

        open (entry.target, entry.effectIndex, entry.title);

        if (! m_windows.empty() && sameTarget (m_windows.back()->getTarget(), entry.target))
            m_windows.back()->setTopLeftPosition (entry.bounds.getPosition());
    }

    // Effects added since: their plugins run now
    for (const auto& entry : std::exchange (m_pending, {}))
        if (! m_session.getCore().getRunningPlugins (entry.target, entry.effectIndex).empty())
            open (entry.target, entry.effectIndex, entry.title);
}
