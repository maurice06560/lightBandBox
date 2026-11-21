#pragma once

namespace onebox::ui
{

/** A recessed, rounded group box with an optional small-caps title, for grouping controls.
    Lay children out inside getContentBounds() so they clear the title and the border.
*/
class Panel : public juce::Component
{
public:
    explicit Panel (const juce::String& title = {});

    void setTitle (const juce::String& newTitle);
    const juce::String& getTitle() const noexcept { return title; }

    /** The area for children: inside the border, below the title (if any). */
    juce::Rectangle<int> getContentBounds() const;

    static constexpr int padding = 12;

    void paint (juce::Graphics&) override;

private:
    juce::String title;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Panel)
};

} // namespace onebox::ui
