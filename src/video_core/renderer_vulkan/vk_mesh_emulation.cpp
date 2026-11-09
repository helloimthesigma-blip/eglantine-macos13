// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include <algorithm>
#include <array>

#include "shader_recompiler/shader_info.h"
#include "video_core/renderer_vulkan/vk_mesh_emulation.h"
#include "video_core/vulkan_common/vulkan_device.h"
#include "video_core/vulkan_common/vulkan_memory_allocator.h"

namespace Vulkan {

MeshEmulation::MeshEmulation(const Device& device, MemoryAllocator& memory_allocator) {
    const auto make_buffer = [&](VkDeviceSize size, VkBufferUsageFlags usage) {
        return memory_allocator.CreateBuffer(
            VkBufferCreateInfo{
                .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
                .pNext = nullptr,
                .flags = 0,
                .size = size,
                .usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | usage,
                .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
                .queueFamilyIndexCount = 0,
                .pQueueFamilyIndices = nullptr,
            },
            MemoryUsage::DeviceLocal);
    };
    args = make_buffer(ARGS_SIZE, VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT |
                                      VK_BUFFER_USAGE_TRANSFER_DST_BIT);
    list = make_buffer(VkDeviceSize{LIST_GROUPS} * 2 * sizeof(u32), 0);
    payload = make_buffer(PAYLOAD_SIZE, 0);
    vertices = make_buffer(VERTICES_SIZE, 0);

    std::array<VkDescriptorSetLayoutBinding, Shader::MESH_EMU_NUM_BUFFERS> bindings{};
    for (u32 index = 0; index < bindings.size(); ++index) {
        bindings[index] = {
            .binding = index,
            .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
            .descriptorCount = 1,
            .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT | VK_SHADER_STAGE_VERTEX_BIT,
            .pImmutableSamplers = nullptr,
        };
    }
    const vk::Device& dev{device.GetLogical()};
    set_layout = dev.CreateDescriptorSetLayout({
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
        .pNext = nullptr,
        .flags = 0,
        .bindingCount = static_cast<u32>(bindings.size()),
        .pBindings = bindings.data(),
    });
    const VkDescriptorPoolSize pool_size{
        .type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
        .descriptorCount = Shader::MESH_EMU_NUM_BUFFERS,
    };
    pool = dev.CreateDescriptorPool({
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
        .pNext = nullptr,
        .flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT,
        .maxSets = 1,
        .poolSizeCount = 1,
        .pPoolSizes = &pool_size,
    });
    const VkDescriptorSetLayout layout{*set_layout};
    sets = pool.Allocate({
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
        .pNext = nullptr,
        .descriptorPool = *pool,
        .descriptorSetCount = 1,
        .pSetLayouts = &layout,
    });
    set = sets[0];

    const std::array<VkDescriptorBufferInfo, Shader::MESH_EMU_NUM_BUFFERS> infos{{
        {*args, 0, VK_WHOLE_SIZE},
        {*list, 0, VK_WHOLE_SIZE},
        {*payload, 0, VK_WHOLE_SIZE},
        {*vertices, 0, VK_WHOLE_SIZE},
    }};
    std::array<VkWriteDescriptorSet, Shader::MESH_EMU_NUM_BUFFERS> writes{};
    for (u32 index = 0; index < writes.size(); ++index) {
        writes[index] = {
            .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
            .pNext = nullptr,
            .dstSet = set,
            .dstBinding = index,
            .dstArrayElement = 0,
            .descriptorCount = 1,
            .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
            .pImageInfo = nullptr,
            .pBufferInfo = &infos[index],
            .pTexelBufferView = nullptr,
        };
    }
    dev.UpdateDescriptorSets(writes, {});
}

MeshEmulation::~MeshEmulation() = default;

Shader::MeshEmulation MeshEmulation::MakeParams(const Shader::MeshInfo& mesh, bool has_task) {
    Shader::MeshEmulation params{};
    params.enabled = true;
    params.has_task = has_task;
    params.primitive_vertices = mesh.PrimitiveVertices();
    params.group_vertices = (std::max)(mesh.max_primitives, 1U) * mesh.PrimitiveVertices();
    params.record_words = 1 + static_cast<u32>(mesh.vertex_lines.size() +
                                                mesh.primitive_lines.size());
    const u64 group_words{u64{params.group_vertices} * params.record_words};
    params.max_groups = static_cast<u32>((std::min)(
        u64{LIST_GROUPS}, VERTICES_SIZE / sizeof(u32) / (std::max)(group_words, u64{1})));
    params.payload_words = (std::max)(mesh.payload_words, 1U);
    params.max_task_groups =
        static_cast<u32>(PAYLOAD_SIZE / sizeof(u32) / params.payload_words);
    return params;
}

} // namespace Vulkan
