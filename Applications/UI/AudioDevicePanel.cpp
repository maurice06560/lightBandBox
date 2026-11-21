#include "AudioDevicePanel.h"

class AudioDevicePanel::DeviceList final : public juce::Component
{
public:
    struct Device
    {
        juce::String typeName, name;
        bool hasInputs = false, hasOutputs = false;
    };

    explicit DeviceList (AudioDevicePanel& owner) : m_owner (owner) {}

    static constexpr int rowHeight = 30;

    void setDevices (std::vector<Device> devices, const MBoxDeviceInfo& current)
    {
        m_devices = std::move (devices);
        m_current = current;
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
            const auto inUse = device.typeName == juce::String (m_current.m_type)
                            && ((device.hasOutputs && device.name == juce::String (m_current.m_outputDevice))
                                || (device.hasInputs && device.name == juce::String (m_current.m_inputDevice)));

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
            g.drawText (device.typeName + "  " + kind + (inUse ? "   in use" : ""), row.reduced (14.0f, 0.0f), juce::Justification::centredRight, false);
        }
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        const auto index = e.y / rowHeight;

        if (index < 0 || index >= (int) m_devices.size())
            return;

        auto& session = m_owner.m_session;

        if (! session.canEdit())
            return;

        // A device with inputs and outputs replaces both; otherwise only its side changes (when
        // the other side is of the same type)
        const auto device = m_devices[(size_t) index];
        const auto sameType = device.typeName == juce::String (m_current.m_type);
        const auto input = device.hasInputs ? device.name : sameType ? juce::String (m_current.m_inputDevice) : juce::String();
        const auto output = device.hasOutputs ? device.name : sameType ? juce::String (m_current.m_outputDevice) : juce::String();
        const auto result = session.getCore().setAudioDevice (device.typeName.toStdString(), input.toStdString(), output.toStdString());

        session.report (result.wasOk() ? juce::Result::ok() : juce::Result::fail (juce::String (result.m_message)), device.name + " selected");
        m_owner.refresh();
    }

private:
    AudioDevicePanel& m_owner;
    std::vector<Device> m_devices;
    MBoxDeviceInfo m_current;
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
        auto& core = m_session.getCore();
        const auto size = m_buffer.getSelectedId();

        if (size <= 0 || size == core.getCurrentDevice().m_bufferSize)
            return;

        if (m_session.canEdit())
        {
            const auto result = core.setBufferSize (size);
            m_session.report (result.wasOk() ? juce::Result::ok() : juce::Result::fail (juce::String (result.m_message)),
                              "Buffer size " + juce::String (size) + " samples");
        }

        refresh();
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
    auto& core = m_session.getCore();
    const auto current = core.getCurrentDevice();

    m_output.setText (current.m_outputDevice.empty() ? juce::String ("(none)") : juce::String (current.m_outputDevice), juce::dontSendNotification);
    m_input.setText (current.m_inputDevice.empty() ? juce::String ("(none)") : juce::String (current.m_inputDevice), juce::dontSendNotification);
    m_details.setText (juce::String (current.m_type) + "   " + juce::String (current.m_sampleRate, 0) + " Hz, "
                           + juce::String (current.m_numInputs) + " in / " + juce::String (current.m_numOutputs) + " out",
                       juce::dontSendNotification);

    // Each size with what one buffer lasts at the current rate
    m_buffer.clear (juce::dontSendNotification);
    const auto rate = juce::jmax (1.0, current.m_sampleRate);

    for (auto size : core.getAvailableBufferSizes())
        m_buffer.addItem (juce::String (size) + " samples  (" + juce::String (1000.0 * size / rate, 1) + " ms)", size);

    m_buffer.setSelectedId (current.m_bufferSize, juce::dontSendNotification);

    // Every device of every type, once, with the sides it has
    std::vector<DeviceList::Device> devices;

    for (const auto& type : core.getAvailableDeviceTypes())
    {
        auto add = [&] (const std::string& name, bool input)
        {
            auto it = std::find_if (devices.begin(), devices.end(), [&] (const auto& d)
                                    { return d.typeName == juce::String (type) && d.name == juce::String (name); });

            if (it == devices.end())
                it = devices.insert (devices.end(), { juce::String (type), juce::String (name) });

            (input ? it->hasInputs : it->hasOutputs) = true;
        };

        for (const auto& name : core.getAvailableDeviceNames (type, false))
            add (name, false);

        for (const auto& name : core.getAvailableDeviceNames (type, true))
            add (name, true);
    }

    m_list->setDevices (std::move (devices), current);
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
