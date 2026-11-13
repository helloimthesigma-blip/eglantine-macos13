// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdlib>

#include "common/common_types.h"

namespace Service::Nx2OotTime {

// Ocarina of Time's frame loop (main+0x6e6be0) computes each frame's delta as 60 / target fps and
// never looks at the clock, so everything runs in slow motion below that rate. The built-in patch
// in patch_manager.cpp makes the loop load the delta from a word in the zeros after .text, and the
// present path writes the measured frame time there.
inline constexpr u64 ProgramId = 0x0400486024DEC000ULL;
inline constexpr u64 DeltaOffset = 0x3C461E0;
inline constexpr u64 LoadDeltaOffset = 0x3C461E4;
inline constexpr u64 ReturnOffset = 0x3C461E8;
inline constexpr u64 HookOffset = 0x6E6CC4;
inline constexpr u32 HookInstruction = 0x14D57D48; // b 0x3c461e4

/// Whether the present path follows real time (EDEN_NX2_OOT_REAL_TIME=0 keeps the game's fixed
/// step of 1.0, the old slow motion, with the patch still in place).
[[nodiscard]] inline bool Enabled() {
    static const bool enabled = [] {
        const char* value = std::getenv("EDEN_NX2_OOT_REAL_TIME");
        return value == nullptr || value[0] != '0';
    }();
    return enabled;
}

/// The sum of the deltas handed to the game, in thousandths, and how many: the game's speed is
/// (sum / 1000) / 60 seconds of game time per wall-clock second elapsed.
inline std::atomic<u64> g_delta_milli_total{0};
inline std::atomic<u64> g_delta_count{0};

/// The largest step as set in the app (Options -> Ocarina of Time Speed); 0 = unset.
inline std::atomic<float> g_max_step_override{0.0f};

/// The game's delta (1.0 = one 60th of a second) for the frame that follows a present.
class FrameDelta {
public:
    using Clock = std::chrono::steady_clock;

    [[nodiscard]] float Update(Clock::time_point now) {
        if (last != Clock::time_point{}) {
            // A pause or a shader stall isn't time the game should catch up on.
            const float seconds = std::chrono::duration<float>(now - last).count();
            const float max_seconds = (std::max)(MaxFrameSeconds, MaxStep() / 60.0f * 1.5f);
            // Much faster frames are followed at once: after a stall (a load) the slow average
            // would otherwise keep the large step for a second of fast frames (up to twice full
            // speed). Ordinary jitter keeps the even smoothing, so the average isn't biased low.
            const float clamped = (std::min)(seconds, max_seconds);
            average_seconds += (clamped - average_seconds) *
                               (clamped < average_seconds * 0.5f ? SmoothingFaster : Smoothing);
        }
        last = now;
        const float delta = std::clamp(average_seconds * 60.0f, 1.0f, MaxStep());
        // Near 60 fps, keep the game's own fixed step instead of following timing jitter.
        return delta < SnapBelow ? 1.0f : delta;
    }

    /// The largest step (EDEN_NX2_OOT_MAX_DELTA, default 4): below 60 / that fps the game slows
    /// down instead of taking bigger steps.
    [[nodiscard]] static float MaxStep() {
        if (const float app = g_max_step_override.load(std::memory_order_relaxed); app >= 1.0f) {
            return (std::min)(app, 30.0f);
        }
        static const float max_step = [] {
            const char* value = std::getenv("EDEN_NX2_OOT_MAX_DELTA");
            const float parsed = value != nullptr ? std::strtof(value, nullptr) : 0.0f;
            return parsed >= 1.0f ? (std::min)(parsed, 30.0f) : MaxDelta;
        }();
        return max_step;
    }

private:
    static constexpr float MaxFrameSeconds = 0.1f;
    static constexpr float Smoothing = 0.125f;
    static constexpr float SmoothingFaster = 0.5f;
    static constexpr float MaxDelta = 4.0f;
    static constexpr float SnapBelow = 1.1f;

    Clock::time_point last{};
    float average_seconds = 1.0f / 60.0f;
};

} // namespace Service::Nx2OotTime
