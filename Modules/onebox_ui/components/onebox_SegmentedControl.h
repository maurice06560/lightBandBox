#pragma once

namespace onebox::ui
{

/** A row of mutually exclusive segments in a recessed pill, like Logic's
    "Event | Marker | Tempo | Signature" selector. Use it for tabs and modes.
*/
class SegmentedControl : public juce::Component
{
public:
    explicit SegmentedControl (const juce::StringArray& segments = {});

    void setSegments (const juce::StringArray& newSegments);
    const juce::StringArray& getSegments() const noexcept { return segments; }

    void setSelectedIndex (int index, juce::NotificationType = juce::sendNotification);
    int getSelectedIndex() const noexcept { return selected; }

    /** Called with the new index when the user picks a segment (or setSelectedIndex notifies). */
    std::function<void (int)> onChange;

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;

private:
    int segmentAt (juce::Point<int>) const;
    juce::Rectangle<float> segmentBounds (int index) const;

    juce::StringArray segments;
    int selected = 0;
    int hovered = -1;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SegmentedControl)
};

} // namespace onebox::ui
