// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common/common_types.h"
#include "shader_recompiler/runtime_info.h"
#include "video_core/vulkan_common/vulkan_wrapper.h"

namespace Shader {
struct MeshInfo;
}

namespace Vulkan {

class Device;
class MemoryAllocator;

/// Buffers for running task and mesh programs as compute shaders on devices without
/// VK_EXT_mesh_shader (MoltenVK). See Shader::MeshEmulation for their layout. Draws use them one
/// at a time, with barriers in between, so one set of buffers serves every draw.
class MeshEmulation {
public:
    static constexpr u32 LIST_GROUPS = 16384;
    static constexpr VkDeviceSize ARGS_SIZE = 64;
    static constexpr VkDeviceSize PAYLOAD_SIZE = 4ULL << 20;
    static constexpr VkDeviceSize VERTICES_SIZE = 64ULL << 20;

    explicit MeshEmulation(const Device& device, MemoryAllocator& memory_allocator);
    ~MeshEmulation();

    /// The compile-time parameters of an emulated task/mesh pair.
    [[nodiscard]] static Shader::MeshEmulation MakeParams(const Shader::MeshInfo& mesh,
                                                          bool has_task);

    [[nodiscard]] VkDescriptorSetLayout SetLayout() const noexcept {
        return *set_layout;
    }
    [[nodiscard]] VkDescriptorSet Set() const noexcept {
        return set;
    }
    [[nodiscard]] VkBuffer ArgsBuffer() const noexcept {
        return *args;
    }

private:
    vk::Buffer args;
    vk::Buffer list;
    vk::Buffer payload;
    vk::Buffer vertices;
    vk::DescriptorSetLayout set_layout;
    vk::DescriptorPool pool;
    vk::DescriptorSets sets;
    VkDescriptorSet set{};
};

} // namespace Vulkan
