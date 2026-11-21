#pragma once

#include "Session.h"
#include "UIHelpers.h"

/** Left sidebar: playlists and their songs. Click to select, double-click to rename,
    right-click for more; the buttons underneath add, rename and delete.
*/
class BrowserPanel final : public ui::Panel,
                           private Session::Listener
{
public:
    explicit BrowserPanel (Session& session);
    ~BrowserPanel() override;

    void resized() override;

private:
    struct Row
    {
        juce::String playlist;
        juce::String song;   // empty for a playlist row
    };

    class RowList;

    void sessionChanged() override;

    /** Faders and mute / solo aren't shown here. */

    void sessionMixChanged() override {}
    void rebuildRows();

    void addPlaylist();
    void addSong();
    void renameRow (const Row& row);
    void deleteRow (const Row& row);
    void showMenu (const Row& row);

    Session& m_session;
    std::vector<Row> m_rows;
    std::unique_ptr<RowList> m_list;
    juce::Viewport m_viewport;

    ui::Button m_addPlaylist { "+ Playlist" };
    ui::Button m_addSong { "+ Song" };
    ui::Button m_rename { "Rename" };
    ui::Button m_delete { "Delete", ui::Button::Variant::Standard };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (BrowserPanel)
};
