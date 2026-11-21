#pragma once

#include "Session.h"
#include "UIHelpers.h"

/** Shows the current audio device and lists every available one to switch to. A device with inputs
    and outputs replaces both; an output-only (or input-only) device replaces just that side. The
    buffer size is chosen here too: smaller means less latency for live inputs, and more CPU load.
    The choice is saved in rig.json and reopened at the next launch.
*/
class AudioDevicePanel final : public juce::Component,
                               private Session::Listener
{
public:
    explicit AudioDevicePanel (Session& session);
    ~AudioDevicePanel() override;

    void refresh();
    void resized() override;

private:
    class DeviceList;

    void sessionChanged() override;
    void sessionMixChanged() override {}

    Session& m_session;
    std::unique_ptr<DeviceList> m_list;
    ui::Panel m_panel { "Audio device" };
    ui::Label m_outputCaption { "Output", ui::Label::Style::Caption };
    ui::Label m_output { {}, ui::Label::Style::Value };
    ui::Label m_inputCaption { "Input", ui::Label::Style::Caption };
    ui::Label m_input { {}, ui::Label::Style::Value };
    ui::Label m_bufferCaption { "Buffer", ui::Label::Style::Caption };
    juce::ComboBox m_buffer;
    ui::Label m_details { {}, ui::Label::Style::Caption };
    ui::Label m_listHeading { "Available devices: click one to use it", ui::Label::Style::Heading };
    ui::Button m_rescan { "Rescan" };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AudioDevicePanel)
};
