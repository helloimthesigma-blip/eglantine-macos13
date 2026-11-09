// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include <algorithm>
#include <cstdlib>
#include <map>

#include <fmt/format.h>

#include "common/logging.h"
#include "video_core/renderer_vulkan/vk_gpu_timing.h"
#include "video_core/renderer_vulkan/vk_master_semaphore.h"
#include "video_core/vulkan_common/vulkan_device.h"

namespace Vulkan {

bool GpuTiming::Enabled() {
    static const bool enabled{std::getenv("EDEN_NX2_GPU_TIMING") != nullptr};
    return enabled;
}

GpuTiming::GpuTiming(const Device& device_) : device{device_} {
    period_ns = device.GetTimestampPeriod();
    pools.resize(4);
    for (size_t index = 0; index < pools.size(); ++index) {
        pools[index].pool = device.GetLogical().CreateQueryPool({
            .sType = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO,
            .pNext = nullptr,
            .flags = 0,
            .queryType = VK_QUERY_TYPE_TIMESTAMP,
            .queryCount = QUERIES,
            .pipelineStatistics = 0,
        });
        if (index != 0) {
            free_pools.push_back(index);
        }
    }
    LOG_INFO(Render_Vulkan, "GPU timing on, timestamp period {} ns", period_ns);
}

GpuTiming::~GpuTiming() = default;

void GpuTiming::Begin(vk::CommandBuffer cmdbuf) {
    Pool& pool{pools[current]};
    if (pool.needs_reset) {
        cmdbuf.ResetQueryPool(*pool.pool, 0, QUERIES);
        pool.needs_reset = false;
    }
    if (open || pool.used + 2 > QUERIES) {
        return;
    }
    cmdbuf.WriteTimestamp(VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, *pool.pool, pool.used++);
    open = true;
}

void GpuTiming::End(vk::CommandBuffer cmdbuf, std::string&& label) {
    if (!open) {
        return;
    }
    Pool& pool{pools[current]};
    cmdbuf.WriteTimestamp(VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, *pool.pool, pool.used++);
    pool.labels.push_back(std::move(label));
    open = false;
}

void GpuTiming::OnSubmit(u64 tick, MasterSemaphore& semaphore) {
    if (open) {
        // A pair split by a submission is dropped.
        --pools[current].used;
        open = false;
    }
    Pool& pool{pools[current]};
    if (pool.used == 0) {
        Collect(semaphore, false);
        return;
    }
    pool.tick = tick;
    pending.push_back(current);
    if (free_pools.empty()) {
        Collect(semaphore, true);
    }
    current = free_pools.front();
    free_pools.pop_front();
    Collect(semaphore, false);
}

void GpuTiming::Collect(MasterSemaphore& semaphore, bool wait) {
    while (!pending.empty()) {
        Pool& pool{pools[pending.front()]};
        if (!semaphore.IsFree(pool.tick)) {
            if (!wait) {
                break;
            }
            semaphore.Wait(pool.tick);
        }
        std::vector<u64> stamps(pool.used);
        const VkResult result{device.GetLogical().GetQueryResults(
            *pool.pool, 0, pool.used, stamps.size() * sizeof(u64), stamps.data(), sizeof(u64),
            VK_QUERY_RESULT_64_BIT)};
        if (result == VK_SUCCESS) {
            for (size_t pair = 0; pair < pool.labels.size(); ++pair) {
                const u64 begin{stamps[pair * 2]};
                const u64 end{stamps[pair * 2 + 1]};
                if (end < begin) {
                    continue;
                }
                const double ms{static_cast<double>(end - begin) * period_ns / 1e6};
                Totals& entry{totals[pool.labels[pair]]};
                entry.ms += ms;
                ++entry.count;
                total_ms += ms;
            }
        }
        pool.used = 0;
        pool.labels.clear();
        pool.needs_reset = true;
        free_pools.push_back(pending.front());
        pending.pop_front();
        wait = false;
    }
    Report();
}

void GpuTiming::Report() {
    const auto now{std::chrono::steady_clock::now()};
    const double seconds{std::chrono::duration<double>(now - window_start).count()};
    if (seconds < 10.0) {
        return;
    }
    std::vector<std::pair<std::string, Totals>> sorted(totals.begin(), totals.end());
    std::ranges::sort(sorted, [](const auto& a, const auto& b) { return a.second.ms > b.second.ms; });
    // Per-second counts by kind and target size (the timestamps may all read the same: MoltenVK
    // on Apple GPUs takes them per command buffer).
    std::map<std::string, double> kinds;
    for (const auto& [label, entry] : sorted) {
        const size_t space{label.find(' ', 3)};
        kinds[label.substr(0, label.rfind(' ', label.find(" draws") != std::string::npos
                                                   ? label.find(" draws")
                                                   : space))] +=
            static_cast<double>(entry.count) / seconds;
    }
    std::string text{fmt::format("GPU timing per second: {:.1f} ms in passes and dispatches",
                                 total_ms / seconds)};
    for (const auto& [kind, count] : kinds) {
        if (count >= 1.0) {
            text += fmt::format("\n  {:7.1f}/s  {}", count, kind);
        }
    }
    for (size_t index = 0; index < (std::min)(sorted.size(), size_t{30}); ++index) {
        const auto& [label, entry] = sorted[index];
        text += fmt::format("\n  {:7.2f} ms/s {:6.1f}/s  {}", entry.ms / seconds,
                            static_cast<double>(entry.count) / seconds, label);
    }
    LOG_INFO(Render_Vulkan, "{}", text);
    totals.clear();
    total_ms = 0.0;
    window_start = now;
}

} // namespace Vulkan
