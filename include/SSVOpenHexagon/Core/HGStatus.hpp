// Copyright (c) 2013-2020 Vittorio Romeo
// License: Academic Free License ("AFL") v. 3.0
// AFL License page: https://opensource.org/licenses/AFL-3.0

#pragma once

#include <SFML/Graphics/Color.hpp>

#include <chrono>
#include <string>

namespace hg {

enum class StateChange
{
    None,
    MustRestart,
    MustReplay
};

struct HexagonGameStatus
{
public:
    using Clock = std::chrono::high_resolution_clock;
    using TimePoint = std::chrono::time_point<Clock>;

private:
    double totalFrametimeAccumulator{};  // Total time (including pauses)
    double playedFrametimeAccumulator{}; // Played time (no pauses)
    double pausedFrametimeAccumulator{}; // Paused time (only pauses)
    double currentPause{0.1 * 60};       // Current pause time
    double currentIncrementTime{};       // Time since last increment
    float customScore{};                 // Value for alternative scoring
    int32_t musicPointer{};              // Current time in level music

public:
    float pulse{75};
    float pulseDirection{1};
    float pulseDelay{0};
    float beatPulse{0};
    float beatPulseDelay{0};
    float pulse3D{1.f};
    float pulse3DDirection{1};
    float flashEffect{0};
    float radius{75};
    float fastSpin{0};
    bool hasDied{false};
    StateChange mustStateChange{StateChange::None};
    bool scoreInvalid{false};
    std::string invalidReason{""};
    bool started{false};
    std::string restartInput;
    std::string replayInput;

    // Reset all the time points and signal that we started
    void start() noexcept;

    // Number of seconds that have passed since last increment
    [[nodiscard]] double getIncrementTimeSeconds() noexcept;

    // Game timer, in seconds
    [[nodiscard]] double getTimeSeconds() noexcept;

    // Absolute time, as time point
    [[nodiscard]] TimePoint getCurrentTP() noexcept;

    // Game timer, as time point
    [[nodiscard]] TimePoint getTimeTP() noexcept;

    // Level start, as time point
    [[nodiscard]] TimePoint getLevelStartTP() noexcept;

    // `true` if we are currently paused
    [[nodiscard]] bool isTimePaused() noexcept;

    // Start a new pause or extend the current pause by `seconds`
    void pauseTime(const double seconds) noexcept;

    // Reset the increment time to the last non-pause time point
    void resetIncrementTime() noexcept;

    // Resets the current time and increment time
    void resetTime();

    // Accumulate the time spent in a frame into the total
    void accumulateFrametime(const double ft) noexcept;

    // Get the current music time in milliseconds
    [[nodiscard]] int32_t getMusicTime() noexcept;

    // Get the current music time in seconds
    [[nodiscard]] double getMusicTimeSeconds() noexcept;

    // Update the music pointer with the new time
    void updateMusicTime(int32_t newTime) noexcept;

    // Update the custom score
    void updateCustomScore(const float score) noexcept;

    // Get total accumulated frametime
    [[nodiscard]] double getTotalAccumulatedFrametime() const noexcept;

    // Get total accumulated frametime, in seconds
    [[nodiscard]] double getTotalAccumulatedFrametimeInSeconds() const noexcept;

    // Get played accumulated frametime
    [[nodiscard]] double getPlayedAccumulatedFrametime() const noexcept;

    // Get played accumulated frametime, in seconds
    [[nodiscard]] double
    getPlayedAccumulatedFrametimeInSeconds() const noexcept;

    // Get paused accumulated frametime
    [[nodiscard]] double getPausedAccumulatedFrametime() const noexcept;

    // Get paused accumulated frametime, in seconds
    [[nodiscard]] double
    getPausedAccumulatedFrametimeInSeconds() const noexcept;

    // Get custom score
    [[nodiscard]] float getCustomScore() const noexcept;
};

} // namespace hg
