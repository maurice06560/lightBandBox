#include "AudioDevicePanel.h"
#include "../Audio/MBoxAudioEngine.h"

class AudioDevicePanel::DeviceList final : public juce::Component
{
public:
    explicit DeviceList (AudioDevicePanel& owner) : m_owner (owner) {}

    static constexpr int rowHeight = 30;

    void setDevices (std::vector<MBoxAudioEngine::DeviceInfo> devices, juce::String type, juce::String input, juce::String output)
    {
        m_devices = std::move (devices);
        m_type = std::move (type);
        m_input = std::move (input);
        m_output = std::move (output);
        setSize (getWidth(), juce::jmax (40, (int) m_devices.size() * rowHeight));
        repaint();
    }

    void paint (juce::Graphics& g) override
    {
        const auto& palette = ui::getTheme (*this).getPalette();

        for (size_t i = 0; i < m_devices.size(); ++i)
        {
            const auto& device = m_devices[i];
            auto row = juce::Rectangle<float> (0.0f, (float) i * rowHeight, (float) getWidth(), (float) rowHeight - 2.0f);
            const auto inUse = device.typeName == m_type
                            && ((device.hasOutputs && device.name == m_output) || (device.hasInputs && device.name == m_input));

            g.setColour (inUse ? palette.accent.withAlpha (0.3f) : (i % 2 == 0 ? palette.panelFill.brighter (0.03f) : palette.panelFill));
            g.fillRoundedRectangle (row, 4.0f);

            if (inUse)
            {
                g.setColour (palette.accent);
                g.fillRoundedRectangle (row.withWidth (4.0f), 2.0f);
            }

            g.setColour (palette.text.brighter (inUse ? 0.5f : 0.2f));
            g.setFont (ui::LookAndFeel::makeFont (13.0f, inUse));
            g.drawText (device.name, row.reduced (14.0f, 0.0f), juce::Justification::centredLeft, true);

            const auto kind = device.hasInputs && device.hasOutputs ? "input + output" : device.hasInputs ? "input" : "output";
            g.setColour (palette.textDim.brighter (0.3f));
            g.setFont (ui::LookAndFeel::makeFont (11.5f));
            g.drawText (juce::String (kind) + (inUse ? "   in use" : ""), row.reduced (14.0f, 0.0f), juce::Justification::centredRight, false);
        }
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        const auto index = e.y / rowHeight;

        if (index < 0 || index >= (int) m_devices.size())
            return;

        auto& session = m_owner.m_session;
        auto* engine = session.getApp().getAudioEngine();

        if (engine == nullptr || ! session.canEdit())
            return;

        const auto device = m_devices[(size_t) index];
        session.report (engine->selectDevice (device), device.name + " selected");
        m_owner.refresh();
        session.changed();   // input/output channel names changed
    }

private:
    AudioDevicePanel& m_owner;
    std::vector<MBoxAudioEngine::DeviceInfo> m_devices;
    juce::String m_type, m_input, m_output;
};

//==============================================================================
AudioDevicePanel::AudioDevicePanel (Session& session) : m_session (session)
{
    m_list = std::make_unique<DeviceList> (*this);
    addAndMakeVisible (m_panel);

    for (auto* c : std::initializer_list<juce::Component*> { &m_outputCaption, &m_output, &m_inputCaption, &m_input, &m_bufferCaption,
                                                              &m_buffer, &m_details, &m_listHeading, m_list.get(), &m_rescan })
        m_panel.addAndMakeVisible (c);

    m_rescan.onClick = [this] { refresh(); };
    m_buffer.setTooltip ("Smaller buffers lower the latency of live inputs, and load the CPU more");
    m_buffer.onChange = [this]
    {
        auto* engine = m_session.getApp().getAudioEngine();
        const auto size = m_buffer.getSelectedId();

        if (engine == nullptr || size <= 0 || size == engine->getCurrentBufferSize())
            return;

        if (m_session.canEdit())
            m_session.report (engine->setBufferSize (size), "Buffer size " + juce::String (size) + " samples");

        refresh();
        m_session.changed();
    };
    m_session.addListener (this);
    setSize (580, 660);
    refresh();
}

AudioDevicePanel::~AudioDevicePanel()
{
    m_session.removeListener (this);
}

void AudioDevicePanel::sessionChanged()
{
    // Switching device restarts the audio: not while a song plays or is paused.
    const auto unlocked = ! m_session.isEditLocked();
    m_list->setEnabled (unlocked);
    m_list->setAlpha (unlocked ? 1.0f : 0.45f);   // greyed while playback locks device changes
    m_buffer.setEnabled (unlocked);
    m_listHeading.setText (unlocked ? "Available devices: click one to use it" : "Available devices: stop playback to switch",
                           juce::dontSendNotification);
    m_list->repaint();
}

void AudioDevicePanel::refresh()
{
    if (auto r = m_session.getApp().initialiseAudio(); r.failed())
        m_session.report (r);

    auto* engine = m_session.getApp().getAudioEngine();

    if (engine == nullptr)
        return;

    m_output.setText (engine->getCurrentOutputDeviceName().isEmpty() ? juce::String ("(none)") : engine->getCurrentOutputDeviceName(), juce::dontSendNotification);
    m_input.setText (engine->getCurrentInputDeviceName().isEmpty() ? juce::String ("(none)") : engine->getCurrentInputDeviceName(), juce::dontSendNotification);
    m_details.setText (engine->getCurrentDeviceTypeName() + "   " + engine->getDeviceDescription(), juce::dontSendNotification);

    // Each size with what one buffer lasts at the current rate.
    m_buffer.clear (juce::dontSendNotification);
    const auto rate = juce::jmax (1.0, engine->getCurrentSampleRate());

    for (auto size : engine->getAvailableBufferSizes())
        m_buffer.addItem (juce::String (size) + " samples  (" + juce::String (1000.0 * size / rate, 1) + " ms)", size);

    m_buffer.setSelectedId (engine->getCurrentBufferSize(), juce::dontSendNotification);
    m_list->setDevices (engine->getAvailableDevices(), engine->getCurrentDeviceTypeName(),
                        engine->getCurrentInputDeviceName(), engine->getCurrentOutputDeviceName());
}

void AudioDevicePanel::resized()
{
    m_panel.setBounds (getLocalBounds().reduced (16));
    auto area = m_panel.getContentBounds();

    for (auto [caption, value] : { std::pair { &m_outputCaption, &m_output }, std::pair { &m_inputCaption, &m_input } })
    {
        auto row = area.removeFromTop (30);
        caption->setBounds (row.removeFromLeft (70));
        value->setBounds (row);
        area.removeFromTop (6);
    }

    {
        auto row = area.removeFromTop (28);
        m_bufferCaption.setBounds (row.removeFromLeft (70));
        m_buffer.setBounds (row.removeFromLeft (220));
        area.removeFromTop (6);
    }

    m_details.setBounds (area.removeFromTop (22));
    area.removeFromTop (12);
    auto heading = area.removeFromTop (26);
    m_rescan.setBounds (heading.removeFromRight (90));
    m_listHeading.setBounds (heading);
    area.removeFromTop (6);
    m_list->setBounds (area.withHeight (juce::jmin (area.getHeight(), m_list->getHeight())));
}
