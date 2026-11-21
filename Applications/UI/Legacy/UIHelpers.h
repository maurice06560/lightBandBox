#pragma once

#include "../Model/Bar.h"
#include "../Model/BankSlotRef.h"
#include "../Model/MBoxBank.h"

#include <onebox_ui/onebox_ui.h>

#include <span>

namespace ui = onebox::ui;

namespace UIHelpers
{
    /** A bankslot by its names, e.g. "Guitar / Clean": the bank's m_name and the slot's m_name. */
    inline juce::String bankSlotName (std::span<const MBoxBank> banks, BankSlotRef ref)
    {
        if (ref.bank < 0 || ref.bank >= (int) banks.size() || ref.slot < 0 || ref.slot >= MBoxBank::numSlots)
            return ref.toString();

        const auto& bank = banks[(size_t) ref.bank];
        return juce::String (bank.m_name) + " / " + juce::String (bank.m_bankSlots[(size_t) ref.slot].m_name);
    }

    /** Logic-style position text: "9 1 1 0" (bar beat division tick). */
    inline juce::String positionText (const Bar& bar)
    {
        return juce::String (bar.bar) + " " + juce::String (bar.beat) + " " + juce::String (bar.division) + " " + juce::String (bar.tick);
    }

    /** Accepts "9", "9 2", "9 2 1 0" or "9.2.1.0". */
    inline std::optional<Bar> parsePosition (const juce::String& text)
    {
        auto normalised = text.trim();

        while (normalised.contains ("  "))
            normalised = normalised.replace ("  ", " ");

        return Bar::parse (normalised.replaceCharacter (' ', '.'));
    }

    inline juce::String volumeText (float decibels)
    {
        if (std::isinf (decibels))
            return juce::String (juce::CharPointer_UTF8 ("-\xe2\x88\x9e dB"));

        return (decibels > 0.0f ? "+" : "") + juce::String (decibels, 1) + " dB";
    }

    inline juce::String timeText (double seconds)
    {
        seconds = juce::jmax (0.0, seconds);
        const auto minutes = (int) (seconds / 60.0);
        return juce::String (minutes).paddedLeft ('0', 2) + ":" + juce::String (seconds - minutes * 60.0, 1).paddedLeft ('0', 4);
    }

    /** One colour per bank, used for bankslot regions and list badges. None is blue: blue is for audio (WAV) regions. */
    inline juce::Colour bankColour (int bankIndex)
    {
        static const juce::Colour colours[] { juce::Colour (0xffd9a441), juce::Colour (0xff3fb39a), juce::Colour (0xff9b7be0),
                                              juce::Colour (0xffd9706a), juce::Colour (0xff7fbf5a) };
        return colours[(size_t) juce::jlimit (0, 4, bankIndex)];
    }

    /** Regions of a muted track, or of tracks silenced by a solo. */
    inline juce::Colour mutedRegionColour() { return juce::Colour (0xff5f6675); }

    /** Asks for a line of text. onDone runs only when the user confirms with a non-empty value. */
    inline void askForText (const juce::String& title, const juce::String& message, const juce::String& initialText,
                            const juce::String& confirmText, std::function<void (const juce::String&)> onDone)
    {
        auto* window = new juce::AlertWindow (title, message, juce::MessageBoxIconType::NoIcon);
        window->addTextEditor ("text", initialText);
        window->addButton (confirmText, 1, juce::KeyPress (juce::KeyPress::returnKey));
        window->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));

        if (auto* editor = window->getTextEditor ("text"))
            editor->selectAll();

        window->enterModalState (true, juce::ModalCallbackFunction::create ([window, callback = std::move (onDone)] (int result)
        {
            const auto text = window->getTextEditorContents ("text").trim();

            if (result == 1 && text.isNotEmpty())
                callback (text);
        }), true);
    }

    /** Asks a question with up to three buttons; onDone receives the 0-based index of the button pressed. */
    inline void ask (const juce::String& title, const juce::String& message, const juce::StringArray& buttons,
                     std::function<void (int)> onDone)
    {
        auto options = juce::MessageBoxOptions().withIconType (juce::MessageBoxIconType::QuestionIcon)
                                                .withTitle (title)
                                                .withMessage (message);

        for (const auto& button : buttons)
            options = options.withButton (button);

        // JUCE returns 1 for the first button, 0 for the last one, and 2 for the middle of three.
        const auto count = buttons.size();

        juce::AlertWindow::showAsync (options, [count, callback = std::move (onDone)] (int result)
        {
            const auto index = result == 1 ? 0 : result == 0 ? count - 1 : 1;
            callback (index);
        });
    }
}
