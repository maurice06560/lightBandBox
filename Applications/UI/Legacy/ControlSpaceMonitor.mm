#include "ControlSpaceMonitor.h"

#import <AppKit/AppKit.h>

namespace
{
    constexpr unsigned short spaceKeyCode = 49;   // kVK_Space
}

ControlSpaceMonitor::ControlSpaceMonitor (juce::Component& component, std::function<void()> onPress)
{
    juce::Component::SafePointer<juce::Component> safeComponent (&component);

    auto handler = ^NSEvent* (NSEvent* event)
    {
        const auto flags = [event modifierFlags] & NSEventModifierFlagDeviceIndependentFlagsMask;
        const auto modifiers = flags & (NSEventModifierFlagControl | NSEventModifierFlagOption
                                        | NSEventModifierFlagCommand | NSEventModifierFlagShift);

        if ([event keyCode] != spaceKeyCode || modifiers != NSEventModifierFlagControl)
            return event;

        // Only in the component's own window (not in a plugin's or the Banks window).
        auto* peer = safeComponent != nullptr ? safeComponent->getPeer() : nullptr;
        auto* view = peer != nullptr ? (NSView*) peer->getNativeHandle() : nil;

        if (view == nil || [event window] != [view window])
            return event;

        if (! [event isARepeat])
            onPress();

        return nil;   // swallowed: no beep
    };

    m_monitor = [[NSEvent addLocalMonitorForEventsMatchingMask: NSEventMaskKeyDown handler: handler] retain];
}

ControlSpaceMonitor::~ControlSpaceMonitor()
{
    if (m_monitor != nullptr)
    {
        [NSEvent removeMonitor: (id) m_monitor];
        [(id) m_monitor release];
    }
}
