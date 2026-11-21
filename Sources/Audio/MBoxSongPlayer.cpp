#include "MBoxSongPlayer.h"
#include "../Plugins/MBoxPluginHost.h"

#include <algorithm>
#include <cmath>
#include <map>

//==============================================================================
// MBoxMixState
//==============================================================================
void MBoxMixState::setFrom (const MBoxTrackMix& trackMix) noexcept
{
    m_gain = volumeDbToGain (trackMix.m_volume);
    m_balance = (float) trackMix.m_balance;
    m_mute = trackMix.m_mute;
    m_solo = trackMix.m_solo;
}

//==============================================================================
// MBoxSongPlayer
//==============================================================================
std::optional<MBoxSlotRef> MBoxSongPlayer::Track::getSlotAt (std::int64_t frame) const noexcept
{
    if (m_slotChanges.empty())
        return std::nullopt;

    auto it = std::upper_bound (m_slotChanges.begin(), m_slotChanges.end(), frame,
                                [] (std::int64_t value, const auto& change) { return value < change.first; });
    return it == m_slotChanges.begin() ? it->second : std::prev (it)->second;
}

MBoxSongPlayer::Track* MBoxSongPlayer::findTrack (int trackId) noexcept
{
    for (auto& track : m_tracks)
        if (track.m_trackId == trackId)
            return &track;

    return nullptr;
}

//==============================================================================
// Building
//==============================================================================
namespace
{
    // Creates a chain on the message thread, from whichever thread the build runs on
    std::unique_ptr<MBoxEffectChain> createChain (const MBoxPlayerBuildContext& context, const std::vector<MBoxEffect>& effects,
                                                  int numChannels, juce::String& error)
    {
        if (effects.empty())
            return std::make_unique<MBoxEffectChain> (numChannels);

        if (context.m_pluginHost == nullptr)
        {
            error = "No plugin host to create " + effects.front().m_name;
            return nullptr;
        }

        if (juce::MessageManager::getInstance()->isThisTheMessageThread())
            return context.m_pluginHost->createChain (effects, numChannels, context.m_sampleRate, context.m_maxBlockSize, error);

        return context.m_pluginHost->createChainFromLoaderThread (effects, numChannels, context.m_sampleRate,
                                                                  context.m_maxBlockSize, error);
    }

    bool isCancelled (const MBoxPlayerBuildContext& context)
    {
        return context.m_shouldCancel && context.m_shouldCancel();
    }

    // The metronome: a count-in of countInBeats beat-units before the left locator (at the
    // tempo and signature there) unless the metronome is Off, then every beat-unit up to the
    // right locator when it's On. Beat 1 of each bar and the first count-in click are accented.
    void buildMetronome (const MBoxSong& song, MBoxSongPlayer& player)
    {
        const auto& timeline = player.m_timeline;
        const auto leftTicks = timeline.barToTicks (song.getLeftLocator());
        const auto beatLength = timeline.getBeatUnitLengthInFrames (leftTicks);
        player.m_countInFrames = (std::int64_t) std::llround (countInBeats * beatLength);
        player.m_metronomeState = song.getMetronomeState();

        for (int beat = countInBeats; beat >= 1; --beat)
            player.m_countInClicks.push_back ({ player.m_leftFrame - (std::int64_t) std::llround (beat * beatLength),
                                                beat == countInBeats });

        timeline.forEachBeatUnit (leftTicks, timeline.barToTicks (song.getRightLocator()),
                                  [&] (const MBoxTimeline::BeatUnit& beat)
                                  {
                                      player.m_metronomeClicks.push_back ({ timeline.ticksToFrames (beat.ticks), beat.beatInBar == 1 });
                                  });
    }

    // Info clicks: each entry starts a group of "numerator" clicks one beat-unit apart, using
    // the signature at the group's start. Clicks outside the locators are dropped.
    std::vector<MBoxClickEvent> buildInfoClicks (const MBoxInfoTrack& info, const MBoxSongPlayer& player)
    {
        std::vector<MBoxClickEvent> clicks;
        const auto& timeline = player.m_timeline;

        for (const auto& start : info.getInfoClicks())
        {
            const auto signature = timeline.getSignatureAtBar (start.m_bar);
            const auto startTicks = timeline.barToTicks (start);

            for (int beat = 0; beat < signature.m_numerator; ++beat)
            {
                const auto frame = timeline.ticksToFrames (startTicks + (std::int64_t) beat * signature.ticksPerBeatUnit());

                if (frame >= player.m_leftFrame && frame < player.m_rightFrame)
                    clicks.push_back ({ frame, beat == 0 });
            }
        }

        std::sort (clicks.begin(), clicks.end());
        return clicks;
    }

    // Whether a running chain built from one effect list can stand for another: the same
    // plugins in the same order, with the same bypass. The states don't count: the running
    // plugins hold the current ones (the model gets them before a build).
    bool samePlugins (const std::vector<MBoxEffect>& a, const std::vector<MBoxEffect>& b)
    {
        return std::equal (a.begin(), a.end(), b.begin(), b.end(), [] (const MBoxEffect& x, const MBoxEffect& y)
        {
            return x.m_name == y.m_name && x.m_format == y.m_format && x.m_manufacturer == y.m_manufacturer
                && x.m_identifier == y.m_identifier && x.m_uniqueId == y.m_uniqueId && x.m_version == y.m_version
                && x.m_bypassed == y.m_bypassed;
        });
    }

    // How buildSong gets its chains and live inputs
    struct SongBuild
    {
        std::map<std::pair<int, int>, std::shared_ptr<MBoxLiveInput>>& m_liveInputs;
        const MBoxSongPlayer* m_previous { nullptr };  // a single-song rebuild: the running song, to reuse from
        std::vector<MBoxEffectChain*> m_newInserts;    // created by this build, still to prepare
        bool m_needsFullBuild { false };               // a single-song rebuild can't do it
    };

    // Builds one song's player. Without a previous song it creates every chain, and adds live
    // inputs and slot chains as needed. With one (a single-song rebuild) it reuses the inserts
    // and mix states of the tracks whose plugins are unchanged, and only uses the live inputs
    // as they are (they are running): if one is missing, or lacks a slot, m_needsFullBuild is
    // set. Returns nullptr then, or with an error on failure or cancellation.
    std::shared_ptr<MBoxSongPlayer> buildSong (const MBoxSong& song, const MBoxPlayerBuildContext& context,
                                               SongBuild& build, juce::String& error)
    {
        auto player = std::make_shared<MBoxSongPlayer>();
        player->m_name = song.getName();
        player->m_timeline = song.getTimeline();
        player->m_leftFrame = player->m_timeline.barToFrames (song.getLeftLocator());
        player->m_rightFrame = player->m_timeline.barToFrames (song.getRightLocator());
        player->m_endDelayFrames = player->m_timeline.secondsToFrames (song.getEndSongDelay());
        player->m_endSongAction = song.getEndSongAction();
        buildMetronome (song, *player);

        const auto reusing = build.m_previous != nullptr;
        std::map<int, int> tracksPerInput;

        for (const auto& modelTrack : song.getTracks())
        {
            if (isCancelled (context))
            {
                error = "Cancelled";
                return nullptr;
            }

            MBoxSongPlayer::Track track;
            track.m_trackId = modelTrack->getId();
            track.m_type = modelTrack->getType();
            const auto where = "Song " + song.getName() + ", track " + modelTrack->getName() + ": ";

            if (const auto* audio = dynamic_cast<const MBoxAudioTrack*> (modelTrack.get()))
            {
                track.m_startFrame = player->m_timeline.barToFrames (audio->getAudioStart());

                if (audio->getFilename() != juce::File())
                {
                    track.m_clip = context.m_clips.getOrLoad (audio->getFilename(), error, context.m_shouldCancel);

                    if (track.m_clip == nullptr)
                    {
                        error = where + error;
                        return nullptr;
                    }
                }
            }
            else if (const auto* physical = dynamic_cast<const MBoxPhysicalTrack*> (modelTrack.get()))
            {
                track.m_physicalInput = physical->getPhysicalInput();
                track.m_diOutput = physical->getDIOutput();

                const std::pair<int, int> key { track.m_physicalInput, tracksPerInput[track.m_physicalInput]++ };
                auto found = build.m_liveInputs.find (key);

                if (found == build.m_liveInputs.end())
                {
                    if (reusing)
                    {
                        build.m_needsFullBuild = true;
                        return nullptr;
                    }

                    found = build.m_liveInputs.emplace (key, std::make_shared<MBoxLiveInput>()).first;
                }

                auto* liveInput = found->second.get();
                track.m_liveInput = liveInput;

                for (const auto& [position, slot] : physical->getBankSlotChanges())
                {
                    track.m_slotChanges.push_back ({ player->m_timeline.barToFrames (position), slot });

                    if (liveInput->getSlotChain (slot) != nullptr)
                        continue;

                    if (reusing)
                    {
                        build.m_needsFullBuild = true;
                        return nullptr;
                    }

                    auto chain = createChain (context, getBankSlot (context.m_banks, slot).m_effectChain, 1, error);

                    if (chain == nullptr)
                    {
                        error = where + error;
                        return nullptr;
                    }

                    liveInput->addSlotChain (slot, std::move (chain));
                }
            }
            else if (const auto* info = dynamic_cast<const MBoxInfoTrack*> (modelTrack.get()))
            {
                track.m_infoClicks = buildInfoClicks (*info, *player);
            }

            const auto numChannels = track.m_type == TrackType::Audio ? 2 : 1;
            const auto* previousTrack = reusing ? const_cast<MBoxSongPlayer*> (build.m_previous)->findTrack (track.m_trackId) : nullptr;

            if (previousTrack != nullptr && previousTrack->m_type != track.m_type)
                previousTrack = nullptr;

            for (int bus = 1; bus <= numberOfMixBus; ++bus)
            {
                const auto b = (size_t) bus - 1;
                const auto* trackMix = song.getTrackMix (bus, track.m_trackId);
                track.m_insertEffects[b] = trackMix != nullptr ? trackMix->m_inserts : std::vector<MBoxEffect> {};

                if (previousTrack != nullptr && samePlugins (previousTrack->m_insertEffects[b], track.m_insertEffects[b]))
                {
                    track.m_inserts[b] = previousTrack->m_inserts[b];
                }
                else
                {
                    track.m_inserts[b] = createChain (context, track.m_insertEffects[b], numChannels, error);

                    if (track.m_inserts[b] == nullptr)
                    {
                        error = where + error;
                        return nullptr;
                    }

                    build.m_newInserts.push_back (track.m_inserts[b].get());
                }

                // The running mix state keeps its live values (they change in both places)
                if (previousTrack != nullptr)
                {
                    track.m_mix[b] = previousTrack->m_mix[b];
                    continue;
                }

                auto& mix = track.m_mix[b];
                mix = std::make_shared<MBoxMixState>();

                if (trackMix != nullptr)
                    mix->setFrom (*trackMix);

                mix->m_currentGain = mix->m_mute ? 0.0f : mix->m_gain.load();
                mix->m_currentBalance = mix->m_balance;
            }

            player->m_tracks.push_back (std::move (track));
        }

        return player;
    }

    bool isNew (const SongBuild& build, const MBoxEffectChain* chain)
    {
        return std::find (build.m_newInserts.begin(), build.m_newInserts.end(), chain) != build.m_newInserts.end();
    }
}

std::unique_ptr<MBoxPlaylistPlayer> buildPlaylistPlayer (const MBoxPlaylist& playlist,
                                                         const MBoxPlayerBuildContext& context,
                                                         juce::String& error)
{
    auto playlistPlayer = std::make_unique<MBoxPlaylistPlayer>();
    playlistPlayer->m_playlistName = playlist.getName();

    SongBuild build { playlistPlayer->m_liveInputs };
    const auto& songs = playlist.getSongs();

    for (size_t songIndex = 0; songIndex < songs.size(); ++songIndex)
    {
        auto player = buildSong (*songs[songIndex], context, build, error);

        if (player == nullptr)
            return nullptr;

        playlistPlayer->m_songs.push_back (std::move (player));

        if (context.m_onProgress)
            context.m_onProgress (0.8f * (float) (songIndex + 1) / (float) songs.size());
    }

    for (int bus = 1; bus <= numberOfMixBus; ++bus)
    {
        auto& master = playlistPlayer->m_masterChains[(size_t) bus - 1];
        master = createChain (context, context.m_rig.m_mixBuses[(size_t) bus - 1].getMasterEffectChain(), 2, error);

        if (master == nullptr)
        {
            error = "Mix bus " + juce::String (bus) + " master: " + error;
            return nullptr;
        }
    }

    //==========================================================================
    // Prepare every chain, then pad them all to one latency: live slot chains to the largest
    // slot latency, and inserts so that every track path totals m_totalLatency.
    int maxSlotLatency = 0, maxInsertLatency = 0;

    for (auto& [key, liveInput] : playlistPlayer->m_liveInputs)
        for (auto* chain : liveInput->getAllChains())
            chain->prepare (context.m_sampleRate, context.m_maxBlockSize);

    for (auto& [key, liveInput] : playlistPlayer->m_liveInputs)
        maxSlotLatency = juce::jmax (maxSlotLatency, liveInput->getLatencySamples());

    for (auto* inserts : build.m_newInserts)
    {
        inserts->prepare (context.m_sampleRate, context.m_maxBlockSize);
        maxInsertLatency = juce::jmax (maxInsertLatency, inserts->getLatencySamples());
    }

    playlistPlayer->m_maxSlotLatency = maxSlotLatency;
    playlistPlayer->m_maxInsertLatency = maxInsertLatency;
    playlistPlayer->m_totalLatency = maxSlotLatency + maxInsertLatency;

    for (auto& [key, liveInput] : playlistPlayer->m_liveInputs)
        liveInput->prepare (context.m_sampleRate, context.m_maxBlockSize, maxSlotLatency);

    for (auto& player : playlistPlayer->m_songs)
        for (auto& track : player->m_tracks)
            for (auto& inserts : track.m_inserts)
                inserts->setCompensatedLatency (track.m_type == TrackType::Physical ? maxInsertLatency
                                                                                    : playlistPlayer->m_totalLatency);

    for (auto& master : playlistPlayer->m_masterChains)
    {
        master->prepare (context.m_sampleRate, context.m_maxBlockSize);
        master->setCompensatedLatency (master->getLatencySamples());
    }

    //==========================================================================
    // Warm up every plugin now, so the first use in the show makes no noise
    for (auto& [key, liveInput] : playlistPlayer->m_liveInputs)
    {
        if (isCancelled (context))
        {
            error = "Cancelled";
            return nullptr;
        }

        liveInput->warmUp (context.m_warmUpSeconds);
    }

    for (auto* inserts : build.m_newInserts)
        inserts->warmUp (context.m_warmUpSeconds);

    for (auto& master : playlistPlayer->m_masterChains)
        master->warmUp (context.m_warmUpSeconds);

    if (context.m_onProgress)
        context.m_onProgress (1.0f);

    return playlistPlayer;
}

std::unique_ptr<MBoxPlaylistPlayer> rebuildSongPlayer (const MBoxPlaylist& playlist, const juce::String& songName,
                                                       MBoxPlaylistPlayer base, const MBoxPlayerBuildContext& context,
                                                       juce::String& error)
{
    error.clear();
    const auto& songs = playlist.getSongs();
    const auto songIndex = playlist.getSongIndex (songName);

    if (songIndex < 0 || songs.size() != base.m_songs.size())
        return nullptr;

    for (size_t i = 0; i < songs.size(); ++i)
        if (base.m_songs[i] == nullptr || ! songs[i]->getName().equalsIgnoreCase (base.m_songs[i]->m_name))
            return nullptr;

    auto playlistPlayer = std::make_unique<MBoxPlaylistPlayer> (std::move (base));
    playlistPlayer->m_generation = 0;

    SongBuild build { playlistPlayer->m_liveInputs, playlistPlayer->m_songs[(size_t) songIndex].get() };
    auto player = buildSong (*songs[(size_t) songIndex], context, build, error);

    if (player == nullptr)
        return nullptr;

    if (context.m_onProgress)
        context.m_onProgress (0.5f);

    // The new chains fit in the latency the playlist is padded to, or it takes a full build
    for (auto* inserts : build.m_newInserts)
    {
        inserts->prepare (context.m_sampleRate, context.m_maxBlockSize);

        if (inserts->getLatencySamples() > playlistPlayer->m_maxInsertLatency)
            return nullptr;
    }

    for (auto& track : player->m_tracks)
        for (auto& inserts : track.m_inserts)
            if (isNew (build, inserts.get()))
                inserts->setCompensatedLatency (track.m_type == TrackType::Physical ? playlistPlayer->m_maxInsertLatency
                                                                                    : playlistPlayer->m_totalLatency);

    for (auto* inserts : build.m_newInserts)
    {
        if (isCancelled (context))
        {
            error = "Cancelled";
            return nullptr;
        }

        inserts->warmUp (context.m_warmUpSeconds);
    }

    playlistPlayer->m_songs[(size_t) songIndex] = std::move (player);

    if (context.m_onProgress)
        context.m_onProgress (1.0f);

    return playlistPlayer;
}
