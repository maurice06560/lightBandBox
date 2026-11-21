#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

/** Calls onPress when Ctrl + Space is pressed in the component's window.

    On macOS JUCE never delivers Ctrl + Space to keyPressed(): the event's text is a NUL character,
    which JUCE reads as no text, and it only sends key presses per character (the system then beeps).
    This catches the key with an NSEvent local monitor before JUCE sees it, and swallows it.
    Elsewhere it does nothing: keyPressed() gets the key as usual.
*/
class ControlSpaceMonitor final
{
public:
    ControlSpaceMonitor (juce::Component& component, std::function<void()> onPress);
    ~ControlSpaceMonitor();

private:
   #if JUCE_MAC
    void* m_monitor { nullptr };
   #endif

    JUCE_DECLARE_NON_COPYABLE (ControlSpaceMonitor)
};
