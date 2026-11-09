// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <array>
#include <chrono>
#include <deque>
#include <string>
#include <unordered_map>
#include <vector>

#include "common/common_types.h"
#include "video_core/vulkan_common/vulkan_wrapper.h"

namespace Vulkan {

class Device;
class MasterSemaphore;

/// EDEN_NX2_GPU_TIMING=1: host GPU time of each render pass and each dispatch, from timestamp
/// queries around them, summed per label (target size and first programs, or the compute
/// program) and logged every 10 s, busiest first. Runs on the scheduler's worker thread.
class GpuTiming {
public:
    explicit GpuTiming(const Device& device);
    ~GpuTiming();

    [[nodiscard]] static bool Enabled();

    /// Timestamp before a render pass or dispatch (outside render passes).
    void Begin(vk::CommandBuffer cmdbuf);
    /// Timestamp after it, with the label it is counted under.
    void End(vk::CommandBuffer cmdbuf, std::string&& label);
    /// The command buffer was submitted with this tick; reads finished pools.
    void OnSubmit(u64 tick, MasterSemaphore& semaphore);

private:
    static constexpr u32 QUERIES = 8192;

    struct Pool {
        vk::QueryPool pool;
        u32 used{};
        bool needs_reset{true};
        u64 tick{};
        std::vector<std::string> labels;
    };
    struct Totals {
        double ms{};
        u64 count{};
    };

    void Collect(MasterSemaphore& semaphore, bool wait);
    void Report();

    const Device& device;
    double period_ns{};
    std::vector<Pool> pools;
    size_t current{};
    std::deque<size_t> pending;
    std::deque<size_t> free_pools;
    bool open{};
    std::unordered_map<std::string, Totals> totals;
    double total_ms{};
    std::chrono::steady_clock::time_point window_start{std::chrono::steady_clock::now()};
};

} // namespace Vulkan
