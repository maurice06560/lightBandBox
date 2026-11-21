#include "OneBoxBandApplication.h"

#include <MBoxCoreAudio.h>

#include <atomic>
#include <csignal>

namespace
{
    std::atomic<bool> quitSignalReceived { false };

    void onQuitSignal (int)
    {
        quitSignalReceived = true;
    }
}

//==============================================================================
OneBoxBandApplication::OneBoxBandApplication()  = default;
OneBoxBandApplication::~OneBoxBandApplication() = default;

//==============================================================================
const juce::String OneBoxBandApplication::getApplicationName()    { return JUCE_APPLICATION_NAME_STRING; }
const juce::String OneBoxBandApplication::getApplicationVersion() { return JUCE_APPLICATION_VERSION_STRING; }

//==============================================================================
void OneBoxBandApplication::initialise (const juce::String&)
{
    juce::Logger::writeToLog ("OneBoxBandApplication: starting " + getApplicationName() + " " + getApplicationVersion());

    m_core = std::make_unique<MBoxCoreAudio>();

    const auto result = m_core->getInitialisationResult();

    if (! result.wasOk())
    {
        juce::Logger::writeToLog ("OneBoxBandApplication: initialisation: " + juce::String (result.m_message));

        // Another rack already owns the data folder and the device: nothing useful to do.
        // (MBoxCoreAudio reports this as "Another ... already ..."; there is no error code yet.)
        if (result.m_message.rfind ("Another", 0) == 0)
        {
            m_core.reset();
            setApplicationReturnValue (1);
            quit();
            return;
        }
    }

    m_eventListenerId = m_core->addEventListener ([] (const MBoxEvent& event)
    {
        if (event.m_name != "transport")  // reported every 50 ms while playing
            juce::Logger::writeToLog ("OneBoxBandApplication: event " + juce::String (event.m_name)
                                      + " " + juce::String (event.m_json));
    });

    std::signal (SIGINT, onQuitSignal);
    std::signal (SIGTERM, onQuitSignal);
    startTimer (100);

    juce::Logger::writeToLog ("OneBoxBandApplication: running");
}

//==============================================================================
void OneBoxBandApplication::timerCallback()
{
    if (! quitSignalReceived.exchange (false))
        return;

    juce::Logger::writeToLog ("OneBoxBandApplication: quit signal received");
    stopTimer();
    quit();
}

//==============================================================================
void OneBoxBandApplication::shutdown()
{
    stopTimer();

    if (m_core == nullptr)
        return;

    m_core->removeEventListener (m_eventListenerId);
    m_core->save();
    m_core.reset();  // closes the device and the TCP server

    juce::Logger::writeToLog ("OneBoxBandApplication: stopped");
}

//==============================================================================
void OneBoxBandApplication::systemRequestedQuit()
{
    quit();
}

void OneBoxBandApplication::anotherInstanceStarted (const juce::String&)
{
    juce::Logger::writeToLog ("OneBoxBandApplication: another instance was started and refused");
}
