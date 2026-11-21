#pragma once

#include "Session.h"
#include "UIHelpers.h"

/** The popup of a song's end control: stop at the end, or play the next song after a delay. */
class SongEndPopup final : public juce::Component
{
public:
    SongEndPopup (Session& session, juce::String playlist, juce::String song)
        : m_session (session), m_playlist (std::move (playlist)), m_song (std::move (song))
    {
        // Item ids are the seconds + 1 (a ComboBox id can't be 0)
        for (int seconds = 0; seconds <= maxEndSongDelaySeconds; ++seconds)
            m_delay.addItem (juce::String (seconds) + (seconds == 1 ? " second" : " seconds"), seconds + 1);

        if (const auto* current = getSong())
        {
            m_action.setSelectedIndex (current->getEndSongAction() == EndSongAction::Continue ? 1 : 0, juce::dontSendNotification);
            m_delay.setSelectedId (current->getEndSongDelay() + 1, juce::dontSendNotification);
        }

        m_action.onChange = [this] (int) { apply(); };
        m_delay.onChange = [this] { apply(); };
        m_delayLabel.setJustificationType (juce::Justification::centredLeft);

        for (auto* c : std::initializer_list<juce::Component*> { &m_title, &m_action, &m_delayLabel, &m_delay, &m_hint })
            addAndMakeVisible (c);

        updateEnablement();
        setSize (250, 150);
    }

    void resized() override
    {
        auto area = getLocalBounds().reduced (12, 10);
        m_title.setBounds (area.removeFromTop (20));
        area.removeFromTop (6);
        m_action.setBounds (area.removeFromTop (30));
        area.removeFromTop (10);

        auto delayRow = area.removeFromTop (26);
        m_delayLabel.setBounds (delayRow.removeFromLeft (52));
        m_delay.setBounds (delayRow);
        area.removeFromTop (6);
        m_hint.setBounds (area);
    }

private:
    MBoxSong* getSong()
    {
        return m_session.getCore().getSongInstance (m_playlist.toStdString(), m_song.toStdString());
    }

    void apply()
    {
        const auto action = m_action.getSelectedIndex() == 1 ? EndSongAction::Continue : EndSongAction::Stop;
        m_session.setSongEnd (m_playlist, m_song, action, juce::jmax (0, m_delay.getSelectedId() - 1));
        updateEnablement();
    }

    void updateEnablement()
    {
        const auto next = m_action.getSelectedIndex() == 1;
        m_delay.setEnabled (next);
        m_delayLabel.setEnabled (next);
        m_delayLabel.setAlpha (next ? 1.0f : 0.4f);
    }

    Session& m_session;
    const juce::String m_playlist, m_song;
    ui::Label m_title { "At the end of the song", ui::Label::Style::Heading };
    ui::SegmentedControl m_action { { "Stop", "Play next" } };
    ui::Label m_delayLabel { "Delay:", ui::Label::Style::Body };
    juce::ComboBox m_delay;
    ui::Label m_hint { "The last song of a playlist always stops.", ui::Label::Style::Caption };
};
