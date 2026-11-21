#include "BrowserPanel.h"
#include "SongEndPopup.h"

/** The rows, painted directly: playlist headers with their songs indented underneath. */
class BrowserPanel::RowList final : public juce::Component,
                                    public juce::TooltipClient
{
public:
    static constexpr int playlistRowHeight = 32;
    static constexpr int songRowHeight = 28;

    explicit RowList (BrowserPanel& owner) : m_owner (owner) {}

    int rowHeight (const Row& row) const { return row.song.isEmpty() ? playlistRowHeight : songRowHeight; }

    int totalHeight() const
    {
        int height = 4;

        for (const auto& row : m_owner.m_rows)
            height += rowHeight (row);

        return height;
    }

    int rowAt (int y) const
    {
        int top = 4;

        for (size_t i = 0; i < m_owner.m_rows.size(); ++i)
        {
            const auto height = rowHeight (m_owner.m_rows[i]);

            if (y >= top && y < top + height)
                return (int) i;

            top += height;
        }

        return -1;
    }

    void paint (juce::Graphics& g) override
    {
        auto& session = m_owner.m_session;
        const auto& palette = ui::getTheme (*this).getPalette();
        int top = 4;

        for (const auto& row : m_owner.m_rows)
        {
            const auto height = rowHeight (row);
            auto area = juce::Rectangle<int> (0, top, getWidth(), height).toFloat().reduced (2.0f, 1.0f);
            top += height;

            const auto isPlaylist = row.song.isEmpty();
            const auto selected = isPlaylist ? (row.playlist == session.getSelectedPlaylistName() && session.getSelectedSongName().isEmpty())
                                             : (row.playlist == session.getSelectedPlaylistName() && row.song == session.getSelectedSongName());

            // A song row is a little shorter, to make room for its end control on the right.
            if (! isPlaylist)
            {
                paintEndControl (g, row, area.removeFromRight (endControlWidth));
                area.removeFromRight (3.0f);
            }

            if (selected)
            {
                g.setGradientFill (juce::ColourGradient (palette.accent.withAlpha (0.85f), 0.0f, area.getY(),
                                                         palette.accent.darker (0.3f).withAlpha (0.85f), 0.0f, area.getBottom(), false));
                g.fillRoundedRectangle (area, 5.0f);
            }
            else if (isPlaylist)
            {
                g.setColour (juce::Colours::white.withAlpha (0.03f));
                g.fillRoundedRectangle (area, 5.0f);
            }

            const auto textColour = selected ? palette.textOnAccent : isPlaylist ? palette.text.brighter (0.25f) : palette.text;
            auto textArea = area.reduced (isPlaylist ? 10.0f : 28.0f, 0.0f);

            if (isPlaylist)
            {
                // disclosure triangle
                juce::Path triangle;
                triangle.addTriangle (0.0f, 0.0f, 8.0f, 0.0f, 4.0f, 6.0f);
                g.setColour (textColour.withMultipliedAlpha (0.7f));
                g.fillPath (triangle, juce::AffineTransform::translation (textArea.getX(), area.getCentreY() - 3.0f));
                textArea.removeFromLeft (16.0f);

                auto* playlist = session.getApp().getPlaylist (row.playlist);
                const auto count = playlist != nullptr ? (int) playlist->getSongs().size() : 0;

                g.setFont (ui::LookAndFeel::makeFont (13.0f, true).withExtraKerningFactor (0.04f));
                g.drawText (row.playlist, textArea, juce::Justification::centredLeft, true);

                g.setColour (selected ? textColour : palette.textDim.brighter (0.2f));
                g.setFont (ui::LookAndFeel::makeFont (11.0f));
                g.drawText (juce::String (count), textArea, juce::Justification::centredRight, false);
            }
            else
            {
                g.setColour (textColour);
                g.setFont (ui::LookAndFeel::makeFont (13.0f));
                g.drawText (row.song, textArea.withTrimmedRight (14.0f), juce::Justification::centredLeft, true);

                if (session.isSongDirty (row.playlist, row.song))
                {
                    g.setColour (selected ? palette.textOnAccent : palette.accentWarm);
                    g.fillEllipse (juce::Rectangle<float> (6.0f, 6.0f).withCentre ({ area.getRight() - 12.0f, area.getCentreY() }));
                }
            }
        }

        if (m_owner.m_rows.empty())
        {
            g.setColour (palette.textDim);
            g.setFont (ui::LookAndFeel::makeFont (12.0f));
            g.drawFittedText ("No playlists yet.\nCreate one with \"+ Playlist\".", getLocalBounds().reduced (10).withHeight (60),
                              juce::Justification::centred, 3);
        }
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        const auto index = rowAt (e.y);

        if (index < 0)
            return;

        const auto row = m_owner.m_rows[(size_t) index];

        if (! row.song.isEmpty() && endControlBounds (index).contains (e.position))
        {
            m_owner.m_session.selectSong (row.playlist, row.song);
            showEndPopup (row, endControlBounds (index));
            return;
        }

        if (row.song.isEmpty())
            m_owner.m_session.selectPlaylist (row.playlist);
        else
            m_owner.m_session.selectSong (row.playlist, row.song);

        if (e.mods.isPopupMenu())
            m_owner.showMenu (row);
    }

    void mouseDoubleClick (const juce::MouseEvent& e) override
    {
        if (const auto index = rowAt (e.y); index >= 0 && ! endControlBounds (index).contains (e.position))
            m_owner.renameRow (m_owner.m_rows[(size_t) index]);
    }

    juce::String getTooltip() override
    {
        const auto position = getMouseXYRelative();
        const auto index = rowAt (position.y);

        if (index < 0 || m_owner.m_rows[(size_t) index].song.isEmpty() || ! endControlBounds (index).contains (position.toFloat()))
            return {};

        const auto* song = findSong (m_owner.m_rows[(size_t) index]);

        if (song == nullptr)
            return {};

        return song->getEndAction() == MBoxSong::EndAction::stop
                   ? "At its end, this song stops. Click to change."
                   : "At its end, the next song starts after " + juce::String (song->getNextDelay()) + " s. Click to change.";
    }

private:
    static constexpr float endControlWidth = 24.0f;

    const MBoxSong* findSong (const Row& row) const
    {
        auto* playlist = m_owner.m_session.getApp().getPlaylist (row.playlist);
        return playlist != nullptr ? playlist->getSong (row.song) : nullptr;
    }

    /** Where a song row's end control is: the row's right end. */
    juce::Rectangle<float> endControlBounds (int index) const
    {
        int top = 4;

        for (int i = 0; i < index; ++i)
            top += rowHeight (m_owner.m_rows[(size_t) i]);

        const auto area = juce::Rectangle<int> (0, top, getWidth(), rowHeight (m_owner.m_rows[(size_t) index])).toFloat().reduced (2.0f, 1.0f);
        return area.withLeft (area.getRight() - endControlWidth);
    }

    /** A small button showing what the song does at its end: a stop square, or "play next" (a play
        triangle against a bar) in the accent colour.
    */
    void paintEndControl (juce::Graphics& g, const Row& row, juce::Rectangle<float> area) const
    {
        const auto& palette = ui::getTheme (*const_cast<RowList*> (this)).getPalette();
        const auto* song = findSong (row);
        const auto next = song != nullptr && song->getEndAction() == MBoxSong::EndAction::playNext;
        const auto box = area.reduced (1.0f, 3.0f);

        g.setColour (juce::Colours::white.withAlpha (0.05f));
        g.fillRoundedRectangle (box, 4.0f);
        g.setColour (palette.panelBorder.brighter (0.2f));
        g.drawRoundedRectangle (box.reduced (0.5f), 4.0f, 1.0f);

        const auto centre = box.getCentre();

        if (! next)
        {
            g.setColour (palette.textDim.brighter (0.3f));
            g.fillRoundedRectangle (juce::Rectangle<float> (7.0f, 7.0f).withCentre (centre), 1.0f);
            return;
        }

        juce::Path icon;
        icon.addTriangle (centre.x - 4.5f, centre.y - 4.5f, centre.x - 4.5f, centre.y + 4.5f, centre.x + 2.0f, centre.y);
        icon.addRectangle (centre.x + 2.5f, centre.y - 4.5f, 2.0f, 9.0f);
        g.setColour (palette.accent);
        g.fillPath (icon);
    }

    void showEndPopup (const Row& row, juce::Rectangle<float> control)
    {
        auto popup = std::make_unique<SongEndPopup> (m_owner.m_session, row.playlist, row.song);
        auto& theme = ui::LookAndFeel::getShared();
        popup->setLookAndFeel (&theme);

        auto& box = juce::CallOutBox::launchAsynchronously (std::move (popup), localAreaToGlobal (control.toNearestInt()), nullptr);
        box.setLookAndFeel (&theme);
    }

    BrowserPanel& m_owner;
};

//==============================================================================
BrowserPanel::BrowserPanel (Session& session)
    : ui::Panel ("Playlists"), m_session (session)
{
    m_list = std::make_unique<RowList> (*this);
    m_viewport.setViewedComponent (m_list.get(), false);
    m_viewport.setScrollBarsShown (true, false);
    m_viewport.setScrollBarThickness (8);
    addAndMakeVisible (m_viewport);

    for (auto* b : { &m_addPlaylist, &m_addSong, &m_rename, &m_delete })
        addAndMakeVisible (b);

    m_addPlaylist.onClick = [this] { addPlaylist(); };
    m_addSong.onClick = [this] { addSong(); };
    m_rename.onClick = [this]
    {
        if (m_session.getSelectedSongName().isNotEmpty())
            renameRow ({ m_session.getSelectedPlaylistName(), m_session.getSelectedSongName() });
        else if (m_session.getSelectedPlaylistName().isNotEmpty())
            renameRow ({ m_session.getSelectedPlaylistName(), {} });
    };
    m_delete.onClick = [this]
    {
        if (m_session.getSelectedSongName().isNotEmpty())
            deleteRow ({ m_session.getSelectedPlaylistName(), m_session.getSelectedSongName() });
        else if (m_session.getSelectedPlaylistName().isNotEmpty())
            deleteRow ({ m_session.getSelectedPlaylistName(), {} });
    };

    m_session.addListener (this);
    sessionChanged();
}

BrowserPanel::~BrowserPanel()
{
    m_session.removeListener (this);
}

void BrowserPanel::resized()
{
    auto area = getContentBounds();

    auto buttons = area.removeFromBottom (72);
    auto firstRow = buttons.removeFromTop (34);
    m_addPlaylist.setBounds (firstRow.removeFromLeft (firstRow.getWidth() / 2).reduced (1, 0));
    m_addSong.setBounds (firstRow.reduced (1, 0));
    buttons.removeFromTop (4);
    m_rename.setBounds (buttons.removeFromLeft (buttons.getWidth() / 2).reduced (1, 0));
    m_delete.setBounds (buttons.reduced (1, 0));

    area.removeFromBottom (8);
    m_viewport.setBounds (area);
    m_list->setSize (m_viewport.getMaximumVisibleWidth(), juce::jmax (area.getHeight(), m_list->totalHeight()));
}

void BrowserPanel::sessionChanged()
{
    rebuildRows();
    m_addSong.setEnabled (m_session.getSelectedPlaylistName().isNotEmpty());
    // Renaming or deleting the playing song's files is refused during playback: grey it out.
    const auto canChange = m_session.getSelectedPlaylistName().isNotEmpty() && ! m_session.isEditLocked();
    m_rename.setEnabled (canChange);
    m_delete.setEnabled (canChange);
    resized();
    m_list->repaint();
}

void BrowserPanel::rebuildRows()
{
    m_rows.clear();

    for (const auto& playlist : m_session.getApp().getPlaylists())
    {
        m_rows.push_back ({ playlist.getName(), {} });

        for (const auto& song : playlist.getSongs())
            m_rows.push_back ({ playlist.getName(), song.getName() });
    }
}

//==============================================================================
void BrowserPanel::addPlaylist()
{
    UIHelpers::askForText ("New playlist", "Name of the new playlist:", {}, "Create",
                           [this] (const juce::String& name) { m_session.createPlaylist (name); });
}

void BrowserPanel::addSong()
{
    const auto playlist = m_session.getSelectedPlaylistName();

    if (playlist.isEmpty())
        return;

    UIHelpers::askForText ("New song", "Name of the new song in " + playlist + ":", {}, "Create",
                           [this, playlist] (const juce::String& name) { m_session.createSong (playlist, name); });
}

void BrowserPanel::renameRow (const Row& row)
{
    const auto isPlaylist = row.song.isEmpty();
    const auto current = isPlaylist ? row.playlist : row.song;

    UIHelpers::askForText (isPlaylist ? "Rename playlist" : "Rename song", "New name for " + current + ":", current, "Rename",
                           [this, row, isPlaylist] (const juce::String& name)
                           {
                               if (isPlaylist)
                                   m_session.renamePlaylist (row.playlist, name);
                               else
                                   m_session.renameSong (row.playlist, row.song, name);
                           });
}

void BrowserPanel::deleteRow (const Row& row)
{
    const auto isPlaylist = row.song.isEmpty();
    const auto message = isPlaylist ? "Delete playlist \"" + row.playlist + "\" and all its songs from disk?"
                                    : "Delete song \"" + row.song + "\" and its files from disk?";

    UIHelpers::ask ("Delete", message + "\nThis cannot be undone.", { "Delete", "Cancel" },
                    [this, row, isPlaylist] (int choice)
                    {
                        if (choice != 0)
                            return;

                        if (isPlaylist)
                            m_session.deletePlaylist (row.playlist);
                        else
                            m_session.deleteSong (row.playlist, row.song);
                    });
}

void BrowserPanel::showMenu (const Row& row)
{
    juce::PopupMenu menu;
    const auto isPlaylist = row.song.isEmpty();

    const auto unlocked = ! m_session.isEditLocked();

    if (isPlaylist)
    {
        menu.addItem ("New song...", [this] { addSong(); });
        menu.addItem ("Save playlist", [this, row] { m_session.savePlaylist (row.playlist); });
        menu.addSeparator();
        juce::PopupMenu::Item copy ("Copy playlist");
        copy.shortcutKeyDescription = "Cmd+C";
        copy.setAction ([this, row] { m_session.copyPlaylist (row.playlist); });
        menu.addItem (std::move (copy));
    }
    else
    {
        menu.addItem ("Save song", [this] { m_session.saveSelectedSong(); });
    }

    juce::PopupMenu::Item paste ("Paste playlist");
    paste.shortcutKeyDescription = "Cmd+V";
    paste.setEnabled (m_session.canPastePlaylist()).setAction ([this] { m_session.pastePlaylist(); });
    menu.addItem (std::move (paste));
    menu.addSeparator();

    menu.addItem ("Rename...", unlocked, false, [this, row] { renameRow (row); });
    menu.addItem ("Delete...", unlocked, false, [this, row] { deleteRow (row); });
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this));
}
