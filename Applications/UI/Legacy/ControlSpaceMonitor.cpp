#include "ControlSpaceMonitor.h"

// macOS has its own implementation (ControlSpaceMonitor.mm); elsewhere keyPressed() gets Ctrl + Space.
#if ! JUCE_MAC
ControlSpaceMonitor::ControlSpaceMonitor (juce::Component&, std::function<void()>) {}
ControlSpaceMonitor::~ControlSpaceMonitor() = default;
#endif
