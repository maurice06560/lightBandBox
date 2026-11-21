#pragma once

#include <Model/MBoxBank.h>
#include <Model/MBoxSong.h>

#include <onebox_ui/onebox_ui.h>

#include <optional>
#include <span>

namespace ui = onebox::ui;

namespace UIHelpers
{
    /** A bankslot by its names, e.g. "Guitar / Clean": the bank's m_name and the slot's m_name. */
    inline juce::String bankSlotName (std::span<const MBoxBank> banks, MBoxSlotRef ref)
    {
        if (! ref.isValid() || ref.m_bank > (int) banks.size())
            return "Bank " + juce::String (ref.m_bank) + " / Slot " + juce::String (ref.m_slot);

        const auto& bank = banks[(size_t) ref.m_bank - 1];
        return bank.m_name + " / " + bank.m_bankSlots[(size_t) ref.m_slot - 1].m_name;
    }

    /** The effects of a bankslot, "dry" when it has none. */
    inline juce::String bankSlotEffects (std::span<const MBoxBank> banks, MBoxSlotRef ref, const juce::String& separator = ", ")
    {
        if (! ref.isValid() || ref.m_bank > (int) banks.size())
            return "dry";

        juce::StringArray names;

        for (const auto& effect : banks[(size_t) ref.m_bank - 1].m_bankSlots[(size_t) ref.m_slot - 1].m_effectChain)
            names.add (effect.m_name);

        return names.isEmpty() ? juce::String ("dry") : names.joinIntoString (separator);
    }

    /** Logic-style position text: "9 1 1 0" (bar beat division tick). */
    inline juce::String positionText (const Bar& bar)
    {
        return juce::String (bar.m_bar) + " " + juce::String (bar.m_beat) + " " + juce::String (bar.m_division) + " " + juce::String (bar.m_tick);
    }

    /** Accepts "9", "9 2", "9 2 1 0" or "9.2.1.0"; missing fields are the start of the bar or beat. */
    inline std::optional<Bar> parsePosition (const juce::String& text)
    {
        auto fields = juce::StringArray::fromTokens (text.trim().replaceCharacter ('.', ' '), " ", {});
        fields.removeEmptyStrings();

        if (fields.isEmpty() || fields.size() > 4)
            return std::nullopt;

        for (const auto& field : fields)
            if (! field.containsOnly ("0123456789"))
                return std::nullopt;

        Bar bar;
        bar.m_bar = fields[0].getIntValue();
        bar.m_beat = fields.size() > 1 ? fields[1].getIntValue() : 1;
        bar.m_division = fields.size() > 2 ? fields[2].getIntValue() : 1;
        bar.m_tick = fields.size() > 3 ? fields[3].getIntValue() : 0;
        return bar;
    }

    inline juce::String signatureText (const Signature& signature)
    {
        return juce::String (signature.m_numerator) + "/" + juce::String (signature.m_denominator);
    }

    /** "7/8" -> 7/8, if valid. */
    inline std::optional<Signature> parseSignature (const juce::String& text)
    {
        const auto numerator = text.upToFirstOccurrenceOf ("/", false, false).trim();
        const auto denominator = text.fromFirstOccurrenceOf ("/", false, false).trim();

        if (! numerator.containsOnly ("0123456789") || ! denominator.containsOnly ("0123456789")
            || numerator.isEmpty() || denominator.isEmpty())
            return std::nullopt;

        const Signature signature { numerator.getIntValue(), denominator.getIntValue() };
        return signature.isValid() ? std::optional<Signature> (signature) : std::nullopt;
    }

    //==========================================================================
    // Volumes. The model treats minVolumeDb and below as silent; the faders show -inf for it.
    inline float toFaderDb (double volumeDb)
    {
        return volumeDb <= minVolumeDb ? -std::numeric_limits<float>::infinity() : (float) volumeDb;
    }

    inline double fromFaderDb (float decibels)
    {
        return std::isinf (decibels) || decibels <= (float) minVolumeDb ? minVolumeDb : juce::jmin ((double) decibels, maxVolumeDb);
    }

    inline juce::String volumeText (double volumeDb)
    {
        if (volumeDb <= minVolumeDb)
            return juce::String (juce::CharPointer_UTF8 ("-\xe2\x88\x9e dB"));

        return (volumeDb > 0.0 ? "+" : "") + juce::String (volumeDb, 1) + " dB";
    }

    /** Balance -1..+1 as "L 40", "C" or "R 25" (percent). */
    inline juce::String balanceText (double balance)
    {
        const auto percent = juce::roundToInt (std::abs (balance) * 100.0);
        return percent == 0 ? juce::String ("C") : (balance < 0.0 ? "L " : "R ") + juce::String (percent);
    }

    inline juce::String timeText (double seconds)
    {
        seconds = juce::jmax (0.0, seconds);
        const auto minutes = (int) (seconds / 60.0);
        return juce::String (minutes).paddedLeft ('0', 2) + ":" + juce::String (seconds - minutes * 60.0, 1).paddedLeft ('0', 4);
    }

    /** Whether a track is heard in a bus: not muted, and soloed when any track of the bus is. */
    inline bool isAudible (const MBoxSong& song, int bus, int trackId)
    {
        const auto* mix = song.getTrackMix (bus, trackId);

        if (mix == nullptr || mix->m_mute)
            return false;

        const auto& all = song.getMix (bus);
        const auto anySolo = std::any_of (all.begin(), all.end(), [] (const MBoxTrackMix& m) { return m.m_solo; });
        return ! anySolo || mix->m_solo;
    }

    /** "Audio", "Physical" or "Info". */
    inline juce::String trackTypeName (TrackType type)
    {
        return type == TrackType::Audio ? "WAV" : type == TrackType::Physical ? "Physical" : "Info";
    }

    /** One colour per bank (1-based), used for bankslot regions and list badges. None is blue: blue is for audio (WAV) regions. */
    inline juce::Colour bankColour (int bank)
    {
        static const juce::Colour colours[] { juce::Colour (0xffd9a441), juce::Colour (0xff3fb39a), juce::Colour (0xff9b7be0),
                                              juce::Colour (0xffd9706a), juce::Colour (0xff7fbf5a) };
        return colours[(size_t) juce::jlimit (0, 4, bank - 1)];
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
        auto options = juce::MessageBoxOptions().withIconType (juce::MessageBoxIconType::NoIcon)
                                                .withTitle (title)
                                                .withMessage (message);

        // Result codes are the button indices + 1, so 0 (the window closed otherwise) is "the last button"
        for (int i = 0; i < buttons.size(); ++i)
            options = options.withButton (buttons[i], i + 1);

        juce::AlertWindow::showAsync (options, [count = buttons.size(), callback = std::move (onDone)] (int result)
        {
            callback (result == 0 ? count - 1 : result - 1);
        });
    }
}
