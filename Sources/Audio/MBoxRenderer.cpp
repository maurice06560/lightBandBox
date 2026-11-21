#include "MBoxRenderer.h"

#include <cmath>

namespace
{
    constexpr float minusThreeDb = 0.70710678f;

    // Raises a peak meter (single writer: the audio thread)
    void raisePeak (std::atomic<float>& meter, float peak) noexcept
    {
        if (peak > meter.load (std::memory_order_relaxed))
            meter.store (peak, std::memory_order_relaxed);
    }

    // Moves a value towards a target by at most maxStep
    float approach (float current, float target, float maxStep) noexcept
    {
        return current < target ? juce::jmin (target, current + maxStep) : juce::jmax (target, current - maxStep);
    }

    // Constant-power pan for a mono source: -3 dB on each side at the centre
    std::pair<float, float> panGains (float balance) noexcept
    {
        const auto angle = (balance + 1.0f) * juce::MathConstants<float>::pi * 0.25f;
        return { std::cos (angle), std::sin (angle) };
    }

    // Balance for a stereo source: turning towards one side lowers the other
    std::pair<float, float> balanceGains (float balance) noexcept
    {
        return { balance > 0.0f ? 1.0f - balance : 1.0f, balance < 0.0f ? 1.0f + balance : 1.0f };
    }
}

//==============================================================================
MBoxRenderer::MBoxRenderer()
{
    for (int bus = 1; bus <= numberOfMixBus; ++bus)
    {
        const MBoxMixBus defaults (bus);
        setBus (bus, defaults.getOutputs(), 0.0, 0.0, 0.0);
    }

    prepare (supportedSampleRate);
}

MBoxRenderer::~MBoxRenderer() = default;

void MBoxRenderer::prepare (double sampleRate)
{
    m_sampleRate = sampleRate;
    m_fadeFrames = juce::jmax (1, (int) std::lround (transportFadeSeconds * sampleRate));
    m_busBuffers.setSize (numberOfMixBus * 2, maxChunkSize);
    m_trackBuffer.setSize (2, maxChunkSize);
    m_insertBuffer.setSize (2, maxChunkSize);
    m_clickBuffer.setSize (1, maxChunkSize);
    m_clickSounds = std::make_unique<MBoxClickSounds> (sampleRate);
}

//==============================================================================
// Message thread
//==============================================================================
void MBoxRenderer::publish (std::unique_ptr<MBoxPlaylistPlayer> player)
{
    JUCE_ASSERT_MESSAGE_THREAD

    if (m_published != nullptr)
        m_retired.push_back (std::move (m_published));

    // Unloading publishes an empty player: a null pointer would read as "nothing pending"
    m_published = player != nullptr ? std::move (player) : std::make_unique<MBoxPlaylistPlayer>();
    m_published->m_generation = m_nextGeneration++;
    m_pending.store (m_published.get());
    collectGarbage();
}

void MBoxRenderer::collectGarbage()
{
    // A retired player can be freed once the audio thread has acknowledged a newer one
    const auto acknowledged = m_acknowledgedGeneration.load();

    m_retired.erase (std::remove_if (m_retired.begin(), m_retired.end(),
                                     [&] (const auto& player) { return player->m_generation < acknowledged; }),
                     m_retired.end());
}

void MBoxRenderer::pushCommand (CommandType type, int value, std::int64_t frame)
{
    const auto scope = m_commandFifo.write (1);

    if (scope.blockSize1 > 0)
        m_commands[(size_t) scope.startIndex1] = { type, value, frame };
    else
        jassertfalse;  // more than 64 commands waiting: the audio callback isn't running
}

void MBoxRenderer::play()                       { pushCommand (CommandType::Play); }
void MBoxRenderer::pause()                      { pushCommand (CommandType::Pause); }
void MBoxRenderer::stop()                       { pushCommand (CommandType::Stop); }
void MBoxRenderer::playFrom (std::int64_t frame) { pushCommand (CommandType::PlayFrom, 0, frame); }
void MBoxRenderer::selectSong (int songIndex)   { pushCommand (CommandType::SelectSong, songIndex); }

void MBoxRenderer::setBus (int bus, std::pair<int, int> outputs, double masterVolumeDb, double masterBalance, double metronomeVolumeDb,
                           bool metronomeOn)
{
    if (bus < 1 || bus > numberOfMixBus)
        return;

    auto& settings = m_buses[(size_t) bus - 1];
    settings.m_left = outputs.first;
    settings.m_right = outputs.second;
    settings.m_masterGain = volumeDbToGain (masterVolumeDb);
    settings.m_masterBalance = (float) juce::jlimit (-1.0, 1.0, masterBalance);
    settings.m_metronomeGain = metronomeOn ? volumeDbToGain (metronomeVolumeDb) : 0.0f;
}

float MBoxRenderer::takeMasterPeak (int bus) noexcept
{
    return bus >= 1 && bus <= numberOfMixBus ? m_buses[(size_t) bus - 1].m_masterPeak.exchange (0.0f) : 0.0f;
}

float MBoxRenderer::takeMetronomePeak (int bus) noexcept
{
    return bus >= 1 && bus <= numberOfMixBus ? m_buses[(size_t) bus - 1].m_metronomePeak.exchange (0.0f) : 0.0f;
}

MBoxRenderer::Status MBoxRenderer::getStatus() const noexcept
{
    return { (TransportState) m_reportedState.load(), m_reportedSongIndex.load(), m_reportedPosition.load() };
}

//==============================================================================
// Audio thread: commands and transport
//==============================================================================
void MBoxRenderer::takePublishedPlayer() noexcept
{
    auto* player = m_pending.exchange (nullptr);

    if (player == nullptr)
        return;

    m_active = player;
    m_acknowledgedGeneration.store (player->m_generation);

    if (m_active->m_songs.empty())
    {
        m_state = TransportState::Stopped;
        m_songIndex = 0;
        m_position = 0;
        m_gain = m_targetGain = 0.0f;
        return;
    }

    if (m_songIndex >= (int) m_active->m_songs.size())
    {
        m_songIndex = 0;
        m_state = TransportState::Stopped;
        m_gain = m_targetGain = 0.0f;
    }

    if (m_state == TransportState::Stopped)
        m_position = m_active->m_songs[(size_t) m_songIndex]->m_leftFrame;
}

void MBoxRenderer::handleCommands() noexcept
{
    const auto scope = m_commandFifo.read (m_commandFifo.getNumReady());

    auto handle = [this] (const Command& command)
    {
        const bool hasSongs = m_active != nullptr && ! m_active->m_songs.empty();

        switch (command.m_type)
        {
            case CommandType::Play:
                if (! hasSongs)
                    break;

                if (m_state == TransportState::Stopped)
                {
                    const auto& song = *m_active->m_songs[(size_t) m_songIndex];
                    m_position = song.m_leftFrame - song.m_preRollFrames;
                    m_gain = 0.0f;
                }

                m_state = TransportState::Playing;
                m_targetGain = 1.0f;
                m_afterFade = AfterFade::Nothing;
                m_pendingSongIndex = -1;
                break;

            case CommandType::PlayFrom:
                if (! hasSongs)
                    break;

                m_pendingSongIndex = -1;

                if (m_state == TransportState::Playing)
                {
                    m_playFromFrame = command.m_frame;
                    startFadeOut (AfterFade::PlayFrom);
                }
                else
                {
                    startPlayingFrom (command.m_frame);
                }
                break;

            case CommandType::Pause:
                if (m_state == TransportState::Playing)
                    startFadeOut (AfterFade::Pause);
                break;

            case CommandType::Stop:
                if (m_state == TransportState::Playing)
                {
                    startFadeOut (AfterFade::Stop);
                }
                else if (m_state == TransportState::Paused)
                {
                    m_afterFade = AfterFade::Stop;
                    finishFadeOut();
                }
                break;

            case CommandType::SelectSong:
                if (! hasSongs || ! juce::isPositiveAndBelow (command.m_value, (int) m_active->m_songs.size()))
                    break;

                m_pendingSongIndex = command.m_value;

                if (m_state == TransportState::Playing)
                {
                    startFadeOut (AfterFade::Stop);
                }
                else
                {
                    m_afterFade = AfterFade::Stop;
                    finishFadeOut();
                }
                break;
        }
    };

    for (int i = 0; i < scope.blockSize1; ++i)  handle (m_commands[(size_t) (scope.startIndex1 + i)]);
    for (int i = 0; i < scope.blockSize2; ++i)  handle (m_commands[(size_t) (scope.startIndex2 + i)]);
}

// Starts playing the current song at a frame, fading in. The count-in plays only from the left locator.
void MBoxRenderer::startPlayingFrom (std::int64_t frame) noexcept
{
    const auto& song = *m_active->m_songs[(size_t) m_songIndex];
    m_position = frame == song.m_leftFrame ? frame - song.m_preRollFrames : frame;
    m_gain = 0.0f;
    m_state = TransportState::Playing;
    m_targetGain = 1.0f;
    m_afterFade = AfterFade::Nothing;
}

void MBoxRenderer::startFadeOut (AfterFade action) noexcept
{
    m_targetGain = 0.0f;
    m_afterFade = action;
}

// Called when a fade-out has reached silence: pause in place, or stop at the left locator
// (of the newly selected song, when a selection was waiting)
void MBoxRenderer::finishFadeOut() noexcept
{
    m_gain = m_targetGain = 0.0f;

    if (m_afterFade == AfterFade::PlayFrom)
    {
        startPlayingFrom (m_playFromFrame);
        return;
    }

    if (m_afterFade == AfterFade::Pause)
    {
        m_state = TransportState::Paused;
    }
    else
    {
        m_state = TransportState::Stopped;

        if (m_pendingSongIndex >= 0)
            m_songIndex = m_pendingSongIndex;

        m_pendingSongIndex = -1;

        if (m_active != nullptr && juce::isPositiveAndBelow (m_songIndex, (int) m_active->m_songs.size()))
            m_position = m_active->m_songs[(size_t) m_songIndex]->m_leftFrame;
    }

    m_afterFade = AfterFade::Nothing;
}

// The playhead reached the right locator
void MBoxRenderer::handleSongEnd() noexcept
{
    const auto& song = *m_active->m_songs[(size_t) m_songIndex];
    const bool hasNext = m_songIndex + 1 < (int) m_active->m_songs.size();

    if (song.m_endSongAction == EndSongAction::Continue && hasNext)
    {
        const auto& next = *m_active->m_songs[(size_t) m_songIndex + 1];
        m_position = next.m_leftFrame - next.m_preRollFrames - song.m_endDelayFrames;
        ++m_songIndex;
    }
    else
    {
        startFadeOut (AfterFade::Stop);
    }
}

//==============================================================================
// Audio thread: rendering
//==============================================================================
void MBoxRenderer::process (const float* const* inputs, int numInputs, float* const* outputs, int numOutputs, int numFrames) noexcept
{
    takePublishedPlayer();
    handleCommands();

    for (int ch = 0; ch < numOutputs; ++ch)
        if (outputs[ch] != nullptr)
            juce::FloatVectorOperations::clear (outputs[ch], numFrames);

    for (int offset = 0; offset < numFrames; offset += maxChunkSize)
        processChunk (inputs, numInputs, outputs, numOutputs, offset, juce::jmin (maxChunkSize, numFrames - offset));

    m_reportedState = (int) m_state;
    m_reportedSongIndex = m_songIndex;
    m_reportedPosition = m_position;
}

// Splits a chunk at the right locator, so the song end is handled on the exact frame
void MBoxRenderer::processChunk (const float* const* inputs, int numInputs, float* const* outputs, int numOutputs,
                                 int offset, int numFrames) noexcept
{
    while (numFrames > 0)
    {
        if (m_state != TransportState::Playing || m_active == nullptr || m_active->m_songs.empty())
            return;

        auto& song = *m_active->m_songs[(size_t) m_songIndex];

        if (m_position >= song.m_rightFrame && m_targetGain > 0.0f)
        {
            handleSongEnd();
            continue;
        }

        auto segment = numFrames;

        if (m_position < song.m_rightFrame)
            segment = (int) juce::jmin ((std::int64_t) numFrames, song.m_rightFrame - m_position);

        renderSegment (song, inputs, numInputs, outputs, numOutputs, offset, segment);
        applyTransportGain (outputs, numOutputs, offset, segment);
        m_position += segment;
        offset += segment;
        numFrames -= segment;

        if (m_afterFade != AfterFade::Nothing && m_gain <= 0.0f)
            finishFadeOut();
    }
}

void MBoxRenderer::renderSegment (MBoxSongPlayer& song, const float* const* inputs, int numInputs,
                                  float* const* outputs, int numOutputs, int offset, int numFrames) noexcept
{
    m_busBuffers.clear (0, numFrames);

    std::array<bool, numberOfMixBus> soloActive {};

    for (const auto& track : song.m_tracks)
        for (size_t bus = 0; bus < soloActive.size(); ++bus)
            soloActive[bus] = soloActive[bus] || track.m_mix[bus]->m_solo.load (std::memory_order_relaxed);

    for (auto& track : song.m_tracks)
    {
        m_trackBuffer.clear (0, numFrames);
        auto* mono = m_trackBuffer.getWritePointer (0);

        switch (track.m_type)
        {
            case TrackType::Audio:
                renderClip (song, track, numFrames);
                break;

            case TrackType::Physical:
                if (juce::isPositiveAndBelow (track.m_physicalInput, numInputs) && inputs[track.m_physicalInput] != nullptr)
                    juce::FloatVectorOperations::copy (mono, inputs[track.m_physicalInput] + offset, numFrames);

                if (track.m_liveInput != nullptr)
                    track.m_liveInput->process (mono, numFrames, track.getSlotAt (m_position));

                if (juce::isPositiveAndBelow (track.m_diOutput, numOutputs) && outputs[track.m_diOutput] != nullptr)
                    juce::FloatVectorOperations::add (outputs[track.m_diOutput] + offset, mono, numFrames);
                break;

            case TrackType::Info:
                MBoxClickSounds::render (track.m_infoClicks, m_clickSounds->getInfo (false), m_clickSounds->getInfo (true),
                                         m_position, numFrames, mono);
                break;
        }

        for (int bus = 0; bus < numberOfMixBus; ++bus)
            mixIntoBus (track, bus, soloActive[(size_t) bus], numFrames);
    }

    // Metronome, delayed by the playlist latency like every other path
    m_clickBuffer.clear (0, numFrames);
    auto* click = m_clickBuffer.getWritePointer (0);
    MBoxClickSounds::render (song.m_metronomeClicks, m_clickSounds->getMetronome (false), m_clickSounds->getMetronome (true),
                             m_position - m_active->m_totalLatency, numFrames, click);

    const auto maxStep = (float) numFrames / (float) m_fadeFrames;

    for (int bus = 0; bus < numberOfMixBus; ++bus)
    {
        auto& settings = m_buses[(size_t) bus];
        auto* left = m_busBuffers.getWritePointer (bus * 2);
        auto* right = m_busBuffers.getWritePointer (bus * 2 + 1);
        const auto metronomeGain = settings.m_metronomeGain.load (std::memory_order_relaxed) * minusThreeDb;

        juce::FloatVectorOperations::addWithMultiply (left, click, metronomeGain, numFrames);
        juce::FloatVectorOperations::addWithMultiply (right, click, metronomeGain, numFrames);

        if (metronomeGain > 0.0f)
        {
            const auto range = juce::FloatVectorOperations::findMinAndMax (click, numFrames);
            raisePeak (settings.m_metronomePeak, juce::jmax (-range.getStart(), range.getEnd()) * metronomeGain);
        }

        float* busChannels[] = { left, right };
        m_active->m_masterChains[(size_t) bus]->process (busChannels, numFrames);

        // Master volume (smoothed) and balance
        const auto startGain = settings.m_currentMasterGain;
        const auto endGain = approach (startGain, settings.m_masterGain.load (std::memory_order_relaxed), maxStep);
        settings.m_currentMasterGain = endGain;
        const auto [balanceLeft, balanceRight] = balanceGains (settings.m_masterBalance.load (std::memory_order_relaxed));

        const auto leftChannel = settings.m_left.load (std::memory_order_relaxed);
        const auto rightChannel = settings.m_right.load (std::memory_order_relaxed);

        if (! juce::isPositiveAndBelow (leftChannel, numOutputs) || outputs[leftChannel] == nullptr)
            continue;

        auto* outLeft = outputs[leftChannel] + offset;
        const bool isStereo = juce::isPositiveAndBelow (rightChannel, numOutputs) && outputs[rightChannel] != nullptr;
        auto* outRight = isStereo ? outputs[rightChannel] + offset : nullptr;
        float peak = 0.0f;

        for (int i = 0; i < numFrames; ++i)
        {
            const auto gain = startGain + (endGain - startGain) * (float) i / (float) numFrames;

            if (isStereo)
            {
                const auto l = left[i] * gain * balanceLeft;
                const auto r = right[i] * gain * balanceRight;
                outLeft[i] += l;
                outRight[i] += r;
                peak = juce::jmax (peak, std::abs (l), std::abs (r));
            }
            else
            {
                const auto m = (left[i] + right[i]) * minusThreeDb * gain;
                outLeft[i] += m;
                peak = juce::jmax (peak, std::abs (m));
            }
        }

        raisePeak (settings.m_masterPeak, peak);
    }
}

// Copies the part of a WAV inside [left locator, right locator), with a 10 ms fade-in at the
// left locator and a 10 ms fade-out ending at the right locator
void MBoxRenderer::renderClip (const MBoxSongPlayer& song, const MBoxSongPlayer::Track& track, int numFrames) noexcept
{
    if (track.m_clip == nullptr)
        return;

    auto* left = m_trackBuffer.getWritePointer (0);
    auto* right = m_trackBuffer.getWritePointer (1);
    const auto* samples = track.m_clip->m_samples.data();
    const auto clipLength = track.m_clip->m_numFrames;
    constexpr float scale = 1.0f / 32768.0f;

    for (int i = 0; i < numFrames; ++i)
    {
        const auto frame = m_position + i;
        const auto clipFrame = frame - track.m_startFrame;

        if (frame < song.m_leftFrame || frame >= song.m_rightFrame || clipFrame < 0 || clipFrame >= clipLength)
            continue;

        const auto envelope = juce::jmin (1.0f,
                                          (float) (frame - song.m_leftFrame) / (float) m_fadeFrames,
                                          (float) (song.m_rightFrame - 1 - frame) / (float) m_fadeFrames);

        left[i]  = samples[clipFrame * 2] * scale * envelope;
        right[i] = samples[clipFrame * 2 + 1] * scale * envelope;
    }
}

// Runs a track through one bus's inserts, then adds it to the bus with its smoothed volume and
// its pan (mono sources) or balance (stereo sources)
void MBoxRenderer::mixIntoBus (MBoxSongPlayer::Track& track, int busIndex, bool soloActive, int numFrames) noexcept
{
    auto& mix = *track.m_mix[(size_t) busIndex];
    auto& inserts = *track.m_inserts[(size_t) busIndex];
    const auto numChannels = inserts.getNumChannels();

    for (int ch = 0; ch < numChannels; ++ch)
        m_insertBuffer.copyFrom (ch, 0, m_trackBuffer, ch, 0, numFrames);

    inserts.process (m_insertBuffer.getArrayOfWritePointers(), numFrames);

    const bool silenced = mix.m_mute.load (std::memory_order_relaxed)
                       || (soloActive && ! mix.m_solo.load (std::memory_order_relaxed));
    const auto targetGain = silenced ? 0.0f : mix.m_gain.load (std::memory_order_relaxed);
    const auto maxStep = (float) numFrames / (float) m_fadeFrames;

    const auto startGain = mix.m_currentGain;
    const auto endGain = approach (startGain, targetGain, maxStep);
    const auto startBalance = mix.m_currentBalance;
    const auto endBalance = approach (startBalance, mix.m_balance.load (std::memory_order_relaxed), maxStep);
    mix.m_currentGain = endGain;
    mix.m_currentBalance = endBalance;

    if (startGain == 0.0f && endGain == 0.0f)
        return;

    const auto [startLeft, startRight] = numChannels == 1 ? panGains (startBalance) : balanceGains (startBalance);
    const auto [endLeft, endRight] = numChannels == 1 ? panGains (endBalance) : balanceGains (endBalance);

    auto* busLeft = m_busBuffers.getWritePointer (busIndex * 2);
    auto* busRight = m_busBuffers.getWritePointer (busIndex * 2 + 1);
    const auto* sourceLeft = m_insertBuffer.getReadPointer (0);
    const auto* sourceRight = m_insertBuffer.getReadPointer (numChannels - 1);

    float peak = 0.0f;

    for (int i = 0; i < numFrames; ++i)
    {
        const auto t = (float) i / (float) numFrames;
        const auto gain = startGain + (endGain - startGain) * t;
        const auto l = sourceLeft[i]  * gain * (startLeft + (endLeft - startLeft) * t);
        const auto r = sourceRight[i] * gain * (startRight + (endRight - startRight) * t);
        busLeft[i]  += l;
        busRight[i] += r;
        peak = juce::jmax (peak, std::abs (l), std::abs (r));
    }

    raisePeak (mix.m_peak, peak);
}

// The play/pause/stop fade, applied to everything this segment wrote
void MBoxRenderer::applyTransportGain (float* const* outputs, int numOutputs, int offset, int numFrames) noexcept
{
    if (m_gain == 1.0f && m_targetGain == 1.0f)
        return;

    const auto step = 1.0f / (float) m_fadeFrames;
    float gain = m_gain;

    for (int i = 0; i < numFrames; ++i)
    {
        gain = approach (gain, m_targetGain, step);

        for (int ch = 0; ch < numOutputs; ++ch)
            if (outputs[ch] != nullptr)
                outputs[ch][offset + i] *= gain;
    }

    m_gain = gain;
}
