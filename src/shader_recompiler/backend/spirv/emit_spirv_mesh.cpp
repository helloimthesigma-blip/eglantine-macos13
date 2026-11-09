// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

// Switch 2 (NVN) task and mesh programs. They talk to the hardware through the stage buffer
// (ISBE): a task program writes the number of mesh workgroups at byte 4 of its output region and
// its payload after it; each mesh workgroup reads that region (its own index at byte 0) and
// writes its vertices and primitives to an attribute region in groups of 32, one 32-word line per
// attribute, plus the primitive count and 8-bit vertex indices to a map region. Here the task's
// output region is the task payload, the mesh output regions live in workgroup memory, and the
// epilogue (reached by every invocation, see structured_control_flow.cpp) hands them to Vulkan.

#include <algorithm>
#include <span>

#include "shader_recompiler/backend/spirv/emit_spirv.h"
#include "shader_recompiler/backend/spirv/emit_spirv_instructions.h"
#include "shader_recompiler/backend/spirv/spirv_emit_context.h"
#include "shader_recompiler/frontend/ir/attribute.h"

namespace Shader::Backend::SPIRV {
namespace {
void Barrier(EmitContext& ctx) {
    ctx.OpControlBarrier(ctx.Const(static_cast<u32>(spv::Scope::Workgroup)),
                         ctx.Const(static_cast<u32>(spv::Scope::Workgroup)),
                         ctx.Const(static_cast<u32>(spv::MemorySemanticsMask::AcquireRelease |
                                                    spv::MemorySemanticsMask::WorkgroupMemory)));
}

Id LocalInvocation(EmitContext& ctx) {
    return ctx.OpCompositeExtract(ctx.U32[1], ctx.OpLoad(ctx.U32[3], ctx.local_invocation_id), 0U);
}

Id Clamped(EmitContext& ctx, Id index, u32 count) {
    return ctx.OpUMin(ctx.U32[1], index, ctx.Const(count - 1));
}

Id PayloadWord(EmitContext& ctx, Id index) {
    return ctx.OpAccessChain(ctx.mesh_payload_u32, ctx.mesh_payload,
                             Clamped(ctx, index, ctx.mesh_info.payload_words));
}

Id SharedWord(EmitContext& ctx, Id array, Id index) {
    return ctx.OpAccessChain(ctx.mesh_shared_u32, array, index);
}

// Runs body under `if (condition)`.
template <typename Func>
void If(EmitContext& ctx, Id condition, Func&& body) {
    const Id then_label{ctx.OpLabel()};
    const Id merge_label{ctx.OpLabel()};
    ctx.OpSelectionMerge(merge_label, spv::SelectionControlMask::MaskNone);
    ctx.OpBranchConditional(condition, then_label, merge_label);
    ctx.AddLabel(then_label);
    body();
    ctx.OpBranch(merge_label);
    ctx.AddLabel(merge_label);
}

// Copies the lines of element `index` (a vertex or a primitive) from the attribute region.
void CopyLines(EmitContext& ctx, Id index, u32 base, u32 num_lines,
               const std::vector<EmitContext::MeshLineTarget>& targets) {
    // Word of line 0: base + (index / 32) * num_lines * 32 + index % 32.
    const Id group{ctx.OpShiftRightLogical(ctx.U32[1], index, ctx.Const(5U))};
    const Id slot{ctx.OpBitwiseAnd(ctx.U32[1], index, ctx.Const(31U))};
    const Id first{ctx.OpIAdd(ctx.U32[1],
                              ctx.OpIMul(ctx.U32[1], group, ctx.Const(num_lines * 32)),
                              ctx.OpIAdd(ctx.U32[1], slot, ctx.Const(base)))};
    for (u32 line = 0; line < num_lines; ++line) {
        const EmitContext::MeshLineTarget& target{targets[line]};
        if (!Sirit::ValidId(target.variable)) {
            continue;
        }
        const Id word{ctx.OpLoad(
            ctx.U32[1],
            SharedWord(ctx, ctx.mesh_attributes, ctx.OpIAdd(ctx.U32[1], first, ctx.Const(line * 32))))};
        const Id value{target.is_float ? ctx.OpBitcast(ctx.F32[1], word) : word};
        const Id pointer_type{target.is_float ? ctx.output_f32 : ctx.output_u32};
        const Id pointer{target.element == ~0U
                             ? ctx.OpAccessChain(pointer_type, target.variable, index)
                             : ctx.OpAccessChain(pointer_type, target.variable, index,
                                                 ctx.Const(target.element))};
        ctx.OpStore(pointer, value);
    }
}

// OpenGL-style clip space: z = (z + w) / 2, from the stage buffer's position lines.
void ConvertDepth(EmitContext& ctx, Id vertex) {
    const auto& lines{ctx.mesh_info.vertex_lines};
    const auto z_line{std::ranges::find(lines, static_cast<u32>(IR::Attribute::PositionZ))};
    const auto w_line{std::ranges::find(lines, static_cast<u32>(IR::Attribute::PositionW))};
    if (z_line == lines.end() || w_line == lines.end()) {
        return;
    }
    const u32 num_lines{static_cast<u32>(lines.size())};
    const Id group{ctx.OpShiftRightLogical(ctx.U32[1], vertex, ctx.Const(5U))};
    const Id slot{ctx.OpBitwiseAnd(ctx.U32[1], vertex, ctx.Const(31U))};
    const Id first{ctx.OpIAdd(ctx.U32[1], ctx.OpIMul(ctx.U32[1], group, ctx.Const(num_lines * 32)),
                              slot)};
    const auto read = [&](auto line) {
        const u32 offset{static_cast<u32>(line - lines.begin()) * 32};
        return ctx.OpBitcast(ctx.F32[1],
                             ctx.OpLoad(ctx.U32[1], SharedWord(ctx, ctx.mesh_attributes,
                                                               ctx.OpIAdd(ctx.U32[1], first,
                                                                          ctx.Const(offset)))));
    };
    const Id z{ctx.OpFMul(ctx.F32[1], ctx.OpFAdd(ctx.F32[1], read(z_line), read(w_line)),
                          ctx.Const(0.5f))};
    ctx.OpStore(ctx.OpAccessChain(ctx.output_f32, ctx.output_position, vertex, ctx.Const(2U)), z);
}

void TaskEpilogue(EmitContext& ctx) {
    Barrier(ctx);
    const Id count{ctx.OpLoad(ctx.U32[1], PayloadWord(ctx, ctx.Const(1U)))};
    const Id one{ctx.Const(1U)};
    // Marker for OpEmitMeshTasksEXT count, 1, 1, payload (PatchMeshInstructions).
    ctx.OpStore(count, one);
    ctx.OpStore(one, ctx.mesh_payload);
    ctx.mesh_tasks_marker = {count, one, one, ctx.mesh_payload};
}

void MeshEpilogue(EmitContext& ctx) {
    const MeshInfo& mesh{ctx.mesh_info};
    Barrier(ctx);
    const Id count{ctx.OpUMin(ctx.U32[1], ctx.OpLoad(ctx.U32[1], ctx.mesh_primitive_count),
                              ctx.Const(mesh.max_primitives))};
    const Id max_vertices{ctx.Const(mesh.max_vertices)};
    // Marker for OpSetMeshOutputsEXT max_vertices, count (PatchMeshInstructions).
    ctx.OpStore(max_vertices, count);
    ctx.mesh_outputs_marker = {max_vertices, count};

    const Id invocation{LocalInvocation(ctx)};
    const u32 threads{ctx.mesh_threads};
    const bool convert_depth{ctx.runtime_info.convert_depth_mode && !ctx.profile.support_native_ndc &&
                             Sirit::ValidId(ctx.output_position)};
    const u32 vertex_lines{static_cast<u32>(mesh.vertex_lines.size())};
    // Points need a size per vertex: the fixed state one, unless the program writes its own.
    const bool points{mesh.PrimitiveVertices() == 1};
    const Id point_size{ctx.Const(ctx.runtime_info.fixed_state_point_size.value_or(1.0f))};
    for (u32 first = 0; first < mesh.max_vertices; first += threads) {
        const Id vertex{ctx.OpIAdd(ctx.U32[1], invocation, ctx.Const(first))};
        const auto copy = [&] {
            if (points) {
                ctx.OpStore(ctx.OpAccessChain(ctx.output_f32, ctx.mesh_point_size, vertex),
                            point_size);
            }
            CopyLines(ctx, vertex, 0, vertex_lines, ctx.mesh_vertex_targets);
            if (convert_depth) {
                ConvertDepth(ctx, vertex);
            }
        };
        if (first + threads > mesh.max_vertices) {
            If(ctx, ctx.OpULessThan(ctx.U1, vertex, max_vertices), copy);
        } else {
            copy();
        }
    }
    const u32 primitive_lines{static_cast<u32>(mesh.primitive_lines.size())};
    const u32 primitive_vertices{mesh.PrimitiveVertices()};
    for (u32 first = 0; first < mesh.max_primitives; first += threads) {
        const Id primitive{ctx.OpIAdd(ctx.U32[1], invocation, ctx.Const(first))};
        If(ctx, ctx.OpULessThan(ctx.U1, primitive, count), [&] {
            // Vertex indices: map bytes 4 + primitive_vertices * primitive.
            const Id byte{ctx.OpIAdd(
                ctx.U32[1], ctx.OpIMul(ctx.U32[1], primitive, ctx.Const(primitive_vertices)),
                ctx.Const(4U))};
            std::array<Id, 3> indices;
            for (u32 element = 0; element < primitive_vertices; ++element) {
                const Id address{ctx.OpIAdd(ctx.U32[1], byte, ctx.Const(element))};
                indices[element] = ctx.OpLoad(ctx.U32[1], SharedWord(ctx, ctx.mesh_map, address));
            }
            const Id index_type{ctx.U32[primitive_vertices]};
            const Id output_indices{ctx.TypePointer(spv::StorageClass::Output, index_type)};
            ctx.OpStore(ctx.OpAccessChain(output_indices, ctx.mesh_primitive_indices, primitive),
                        primitive_vertices == 1
                            ? indices[0]
                            : ctx.OpCompositeConstruct(
                                  index_type,
                                  std::span<const Id>(indices.data(), primitive_vertices)));
            CopyLines(ctx, primitive, ctx.mesh_primitive_base, primitive_lines,
                      ctx.mesh_primitive_targets);
        });
    }
}

// Mesh emulation (RuntimeInfo::mesh_emulation) ------------------------------------------------

Id EmuWord(EmitContext& ctx, u32 buffer, Id index) {
    return ctx.OpAccessChain(ctx.mesh_emu_u32, ctx.mesh_emu_buffers[buffer], ctx.u32_zero_value,
                             index);
}

Id WorkgroupIndex(EmitContext& ctx) {
    return ctx.OpCompositeExtract(ctx.U32[1], ctx.OpLoad(ctx.U32[3], ctx.workgroup_id), 0U);
}

// for (i = start; i < end; i += step) body(i), with a Private counter (no phis needed).
template <typename Func>
void Loop(EmitContext& ctx, Id start, Id end, u32 step, Func&& body) {
    ctx.OpStore(ctx.mesh_emu_counter, start);
    const Id header{ctx.OpLabel()};
    const Id body_label{ctx.OpLabel()};
    const Id continue_label{ctx.OpLabel()};
    const Id merge_label{ctx.OpLabel()};
    ctx.OpBranch(header);
    ctx.AddLabel(header);
    const Id index{ctx.OpLoad(ctx.U32[1], ctx.mesh_emu_counter)};
    ctx.OpLoopMerge(merge_label, continue_label, spv::LoopControlMask::MaskNone);
    ctx.OpBranchConditional(ctx.OpULessThan(ctx.U1, index, end), body_label, merge_label);
    ctx.AddLabel(body_label);
    body(index);
    ctx.OpBranch(continue_label);
    ctx.AddLabel(continue_label);
    ctx.OpStore(ctx.mesh_emu_counter, ctx.OpIAdd(ctx.U32[1], index, ctx.Const(step)));
    ctx.OpBranch(header);
    ctx.AddLabel(merge_label);
}

Id AtomicAdd(EmitContext& ctx, u32 buffer, u32 word, Id value) {
    return ctx.OpAtomicIAdd(ctx.U32[1], EmuWord(ctx, buffer, ctx.Const(word)),
                            ctx.Const(static_cast<u32>(spv::Scope::Device)), ctx.u32_zero_value,
                            value);
}

// Thread 0 takes list entries for the mesh workgroups the task asks for, as many as fit; then
// the workgroup copies its payload and writes the entries.
void TaskEpilogueEmulated(EmitContext& ctx) {
    const MeshEmulation& emu{ctx.runtime_info.mesh_emulation};
    const u32 words{ctx.mesh_info.payload_words};
    Barrier(ctx);
    const Id task{WorkgroupIndex(ctx)};
    const Id invocation{LocalInvocation(ctx)};
    const Id fits{ctx.OpULessThan(ctx.U1, task, ctx.Const(emu.max_task_groups))};
    const Id requested{ctx.OpSelect(ctx.U32[1], fits,
                                    ctx.OpLoad(ctx.U32[1], PayloadWord(ctx, ctx.Const(1U))),
                                    ctx.u32_zero_value)};
    const Id shared_base{
        ctx.OpAccessChain(ctx.mesh_shared_u32, ctx.mesh_emu_shared, ctx.u32_zero_value)};
    const Id shared_count{ctx.OpAccessChain(ctx.mesh_shared_u32, ctx.mesh_emu_shared, ctx.Const(1U))};
    If(ctx, ctx.OpIEqual(ctx.U1, invocation, ctx.u32_zero_value), [&] {
        // Word 7 allocates list entries; words 0 and 3 count those that fit.
        const Id base{AtomicAdd(ctx, MESH_EMU_ARGS, 7, requested)};
        const Id max_groups{ctx.Const(emu.max_groups)};
        const Id available{ctx.OpSelect(ctx.U32[1], ctx.OpULessThan(ctx.U1, base, max_groups),
                                        ctx.OpISub(ctx.U32[1], max_groups, base),
                                        ctx.u32_zero_value)};
        const Id count{ctx.OpUMin(ctx.U32[1], requested, available)};
        AtomicAdd(ctx, MESH_EMU_ARGS, 0, count);
        AtomicAdd(ctx, MESH_EMU_ARGS, MESH_EMU_ARGS_DRAW_WORD,
                  ctx.OpIMul(ctx.U32[1], count, ctx.Const(emu.group_vertices)));
        ctx.OpStore(shared_base, base);
        ctx.OpStore(shared_count, count);
    });
    Barrier(ctx);
    const Id base{ctx.OpLoad(ctx.U32[1], shared_base)};
    const Id count{ctx.OpLoad(ctx.U32[1], shared_count)};
    const Id payload_base{ctx.OpIMul(ctx.U32[1], task, ctx.Const(words))};
    const Id payload_end{ctx.OpSelect(ctx.U32[1], fits, ctx.Const(words), ctx.u32_zero_value)};
    Loop(ctx, invocation, payload_end, ctx.mesh_threads, [&](Id word) {
        ctx.OpStore(EmuWord(ctx, MESH_EMU_PAYLOAD, ctx.OpIAdd(ctx.U32[1], payload_base, word)),
                    ctx.OpLoad(ctx.U32[1], PayloadWord(ctx, word)));
    });
    Loop(ctx, invocation, count, ctx.mesh_threads, [&](Id index) {
        const Id entry{ctx.OpShiftLeftLogical(ctx.U32[1], ctx.OpIAdd(ctx.U32[1], base, index),
                                              ctx.Const(1U))};
        ctx.OpStore(EmuWord(ctx, MESH_EMU_LIST, entry), task);
        ctx.OpStore(EmuWord(ctx, MESH_EMU_LIST, ctx.OpIAdd(ctx.U32[1], entry, ctx.Const(1U))),
                    index);
    });
}

// Copies lines of element `index` from the attribute region to the vertex buffer at `record`.
void CopyLinesToRecord(EmitContext& ctx, Id index, u32 base, u32 num_lines, Id record) {
    const Id group{ctx.OpShiftRightLogical(ctx.U32[1], index, ctx.Const(5U))};
    const Id slot{ctx.OpBitwiseAnd(ctx.U32[1], index, ctx.Const(31U))};
    const Id first{ctx.OpIAdd(ctx.U32[1],
                              ctx.OpIMul(ctx.U32[1], group, ctx.Const(num_lines * 32)),
                              ctx.OpIAdd(ctx.U32[1], slot, ctx.Const(base)))};
    for (u32 line = 0; line < num_lines; ++line) {
        const Id address{ctx.OpUMin(ctx.U32[1],
                                    ctx.OpIAdd(ctx.U32[1], first, ctx.Const(line * 32)),
                                    ctx.Const(ctx.mesh_attribute_words - 1))};
        const Id word{ctx.OpLoad(ctx.U32[1], SharedWord(ctx, ctx.mesh_attributes, address))};
        ctx.OpStore(EmuWord(ctx, MESH_EMU_VERTICES, ctx.OpIAdd(ctx.U32[1], record, ctx.Const(line))),
                    word);
    }
}

// Each primitive becomes primitive_vertices records: a valid flag, its vertex's lines, then the
// primitive's lines. Primitives past the count are flagged invalid (the vertex shader culls them).
void MeshEpilogueEmulated(EmitContext& ctx) {
    const MeshInfo& mesh{ctx.mesh_info};
    const MeshEmulation& emu{ctx.runtime_info.mesh_emulation};
    Barrier(ctx);
    const Id count{ctx.OpUMin(ctx.U32[1], ctx.OpLoad(ctx.U32[1], ctx.mesh_primitive_count),
                              ctx.Const(mesh.max_primitives))};
    const Id group{ctx.OpLoad(ctx.U32[1], ctx.mesh_emu_group)};
    const Id group_valid{ctx.OpULessThan(ctx.U1, group, ctx.Const(emu.max_groups))};
    const Id invocation{LocalInvocation(ctx)};
    const u32 threads{ctx.mesh_threads};
    const u32 primitive_vertices{mesh.PrimitiveVertices()};
    const u32 vertex_lines{static_cast<u32>(mesh.vertex_lines.size())};
    const u32 primitive_lines{static_cast<u32>(mesh.primitive_lines.size())};
    const Id group_base{ctx.OpIMul(ctx.U32[1], group, ctx.Const(emu.group_vertices))};
    for (u32 first = 0; first < mesh.max_primitives; first += threads) {
        const Id primitive{ctx.OpIAdd(ctx.U32[1], invocation, ctx.Const(first))};
        Id condition{group_valid};
        if (first + threads > mesh.max_primitives) {
            condition = ctx.OpLogicalAnd(
                ctx.U1, condition,
                ctx.OpULessThan(ctx.U1, primitive, ctx.Const(mesh.max_primitives)));
        }
        If(ctx, condition, [&] {
            const Id live{ctx.OpULessThan(ctx.U1, primitive, count)};
            const Id first_vertex{ctx.OpIAdd(
                ctx.U32[1], group_base,
                ctx.OpIMul(ctx.U32[1], primitive, ctx.Const(primitive_vertices)))};
            std::array<Id, 3> records;
            for (u32 element = 0; element < primitive_vertices; ++element) {
                records[element] = ctx.OpIMul(
                    ctx.U32[1], ctx.OpIAdd(ctx.U32[1], first_vertex, ctx.Const(element)),
                    ctx.Const(emu.record_words));
                ctx.OpStore(EmuWord(ctx, MESH_EMU_VERTICES, records[element]),
                            ctx.OpSelect(ctx.U32[1], live, ctx.Const(1U), ctx.u32_zero_value));
            }
            If(ctx, live, [&] {
                const Id byte{ctx.OpIAdd(
                    ctx.U32[1], ctx.OpIMul(ctx.U32[1], primitive, ctx.Const(primitive_vertices)),
                    ctx.Const(4U))};
                for (u32 element = 0; element < primitive_vertices; ++element) {
                    const Id address{ctx.OpIAdd(ctx.U32[1], byte, ctx.Const(element))};
                    const Id vertex{ctx.OpUMin(
                        ctx.U32[1], ctx.OpLoad(ctx.U32[1], SharedWord(ctx, ctx.mesh_map, address)),
                        ctx.Const(mesh.max_vertices - 1))};
                    const Id record{ctx.OpIAdd(ctx.U32[1], records[element], ctx.Const(1U))};
                    CopyLinesToRecord(ctx, vertex, 0, vertex_lines, record);
                    CopyLinesToRecord(ctx, primitive, ctx.mesh_primitive_base, primitive_lines,
                                      ctx.OpIAdd(ctx.U32[1], record, ctx.Const(vertex_lines)));
                }
            });
        });
    }
}

void MeshPrologueEmulated(EmitContext& ctx) {
    // Straight-line code only: the entry block's label is a phi predecessor for later blocks.
    const MeshEmulation& emu{ctx.runtime_info.mesh_emulation};
    const Id group{WorkgroupIndex(ctx)};
    ctx.OpStore(ctx.mesh_emu_group, group);
    if (emu.has_task) {
        const Id entry{ctx.OpShiftLeftLogical(
            ctx.U32[1], ctx.OpUMin(ctx.U32[1], group, ctx.Const(emu.max_groups - 1)),
            ctx.Const(1U))};
        ctx.OpStore(ctx.mesh_emu_task, ctx.OpLoad(ctx.U32[1], EmuWord(ctx, MESH_EMU_LIST, entry)));
        ctx.OpStore(ctx.mesh_emu_local,
                    ctx.OpLoad(ctx.U32[1], EmuWord(ctx, MESH_EMU_LIST,
                                                   ctx.OpIAdd(ctx.U32[1], entry, ctx.Const(1U)))));
    } else {
        ctx.OpStore(ctx.mesh_emu_task, ctx.u32_zero_value);
        ctx.OpStore(ctx.mesh_emu_local, group);
    }
}
} // Anonymous namespace

Id EmitIsbeRead(EmitContext& ctx, Id address) {
    const Id workgroup{
        ctx.OpCompositeExtract(ctx.U32[1], ctx.OpLoad(ctx.U32[3], ctx.workgroup_id), 0U)};
    if (ctx.stage == Stage::Task) {
        return workgroup; // the input region holds the workgroup index (VERTEX_ID)
    }
    const Id index{ctx.OpShiftRightLogical(ctx.U32[1], address, ctx.Const(2U))};
    if (ctx.mesh_emulated) {
        const MeshEmulation& emu{ctx.runtime_info.mesh_emulation};
        const u32 words{ctx.mesh_info.payload_words};
        const Id task{ctx.OpLoad(ctx.U32[1], ctx.mesh_emu_task)};
        const Id word_index{ctx.OpUMin(
            ctx.U32[1],
            ctx.OpIAdd(ctx.U32[1], ctx.OpIMul(ctx.U32[1], task, ctx.Const(words)),
                       Clamped(ctx, index, words)),
            ctx.Const((std::max)(emu.max_task_groups, 1U) * words - 1))};
        const Id word{emu.has_task ? ctx.OpLoad(ctx.U32[1], EmuWord(ctx, MESH_EMU_PAYLOAD, word_index))
                                   : ctx.u32_zero_value};
        return ctx.OpSelect(ctx.U32[1], ctx.OpIEqual(ctx.U1, address, ctx.u32_zero_value),
                            ctx.OpLoad(ctx.U32[1], ctx.mesh_emu_local), word);
    }
    const Id word{ctx.OpLoad(ctx.U32[1], PayloadWord(ctx, index))};
    return ctx.OpSelect(ctx.U32[1], ctx.OpIEqual(ctx.U1, address, ctx.u32_zero_value), workgroup,
                        word);
}

void EmitIsbeWriteAttribute(EmitContext& ctx, Id address, Id value) {
    const Id index{ctx.OpShiftRightLogical(ctx.U32[1], address, ctx.Const(2U))};
    if (ctx.stage == Stage::Task) {
        ctx.OpStore(PayloadWord(ctx, index), value);
        return;
    }
    ctx.OpStore(SharedWord(ctx, ctx.mesh_attributes, Clamped(ctx, index, ctx.mesh_attribute_words)),
                value);
}

void EmitIsbeWriteMapU8(EmitContext& ctx, Id address, Id value) {
    if (ctx.stage != Stage::Mesh) {
        throw NotImplementedException("Stage buffer map write outside mesh programs");
    }
    ctx.OpStore(SharedWord(ctx, ctx.mesh_map, Clamped(ctx, address, ctx.mesh_map_bytes)),
                ctx.OpBitwiseAnd(ctx.U32[1], value, ctx.Const(0xffU)));
}

void EmitIsbeWriteMapU32(EmitContext& ctx, const IR::Value& address, Id value) {
    // The primitive count: the map is written from its end, so its first word is at byte 3.
    if (ctx.stage != Stage::Mesh || !address.IsImmediate() || address.U32() != 3) {
        throw NotImplementedException("32-bit stage buffer map write");
    }
    ctx.OpStore(ctx.mesh_primitive_count, value);
}

void EmitMeshPrologue(EmitContext& ctx) {
    if (ctx.stage == Stage::Task) {
        // A task that culls all its threads never writes the mesh workgroup count, and the payload
        // starts undefined. Intel's garbage there launches enough workgroups to lose the device.
        ctx.OpStore(PayloadWord(ctx, ctx.Const(1U)), ctx.u32_zero_value);
        Barrier(ctx);
        return;
    }
    if (ctx.stage != Stage::Mesh) {
        return;
    }
    if (ctx.mesh_emulated) {
        MeshPrologueEmulated(ctx);
    }
    // Every invocation stores the zero: no branch here, the entry block's label is a phi
    // predecessor for later blocks.
    ctx.OpStore(ctx.mesh_primitive_count, ctx.u32_zero_value);
    Barrier(ctx);
}

void EmitMeshEpilogue(EmitContext& ctx) {
    if (ctx.stage == Stage::Task) {
        ctx.mesh_emulated ? TaskEpilogueEmulated(ctx) : TaskEpilogue(ctx);
    } else if (ctx.stage == Stage::Mesh) {
        ctx.mesh_emulated ? MeshEpilogueEmulated(ctx) : MeshEpilogue(ctx);
    }
}

std::vector<u32> EmitMeshEmulationVertexSPIRV(const Profile& profile,
                                              const RuntimeInfo& runtime_info,
                                              const MeshInfo& mesh) {
    const MeshEmulation& emu{runtime_info.mesh_emulation};
    Sirit::Module m{profile.supported_spirv};
    m.AddCapability(spv::Capability::Shader);
    m.AddExtension("SPV_KHR_storage_buffer_storage_class");
    const Id void_id{m.TypeVoid()};
    const Id u32_id{m.TypeInt(32, false)};
    const Id s32_id{m.TypeInt(32, true)};
    const Id f32_id{m.TypeFloat(32)};
    const Id vec4_id{m.TypeVector(f32_id, 4)};
    const Id bool_id{m.TypeBool()};
    const auto u32c = [&](u32 value) { return m.Constant(u32_id, value); };
    const auto f32c = [&](f32 value) { return m.Constant(f32_id, value); };
    std::vector<Id> interfaces;

    const Id array_type{m.TypeRuntimeArray(u32_id)};
    m.Decorate(array_type, spv::Decoration::ArrayStride, 4U);
    const Id struct_type{m.TypeStruct(array_type)};
    m.Decorate(struct_type, spv::Decoration::Block);
    m.MemberDecorate(struct_type, 0, spv::Decoration::Offset, 0U);
    const Id vertices{m.AddGlobalVariable(m.TypePointer(spv::StorageClass::StorageBuffer, struct_type),
                                          spv::StorageClass::StorageBuffer)};
    m.Decorate(vertices, spv::Decoration::DescriptorSet, MESH_EMU_SET);
    m.Decorate(vertices, spv::Decoration::Binding, MESH_EMU_VERTICES);
    m.Name(vertices, "mesh_emu_vertices");
    if (profile.supported_spirv >= 0x00010400) {
        interfaces.push_back(vertices);
    }
    const Id word_pointer{m.TypePointer(spv::StorageClass::StorageBuffer, u32_id)};

    const auto define = [&](Id type, spv::StorageClass storage, std::optional<spv::BuiltIn> builtin) {
        const Id id{m.AddGlobalVariable(m.TypePointer(storage, type), storage)};
        if (builtin) {
            m.Decorate(id, spv::Decoration::BuiltIn, *builtin);
        }
        interfaces.push_back(id);
        return id;
    };
    const Id vertex_index{define(s32_id, spv::StorageClass::Input, spv::BuiltIn::VertexIndex)};
    const Id position{define(vec4_id, spv::StorageClass::Output, spv::BuiltIn::Position)};

    // Values gathered per output: position, generics, point size, layer and viewport.
    struct Vec4Output {
        Id variable{};
        std::array<Id, 4> components{};
    };
    std::array<Vec4Output, 32> generics{};
    Vec4Output position_value{position, {f32c(0.0f), f32c(0.0f), f32c(0.0f), f32c(1.0f)}};
    Id point_size_value{};
    Id layer_value{};
    Id viewport_value{};

    const Id main{m.OpFunction(void_id, spv::FunctionControlMask::MaskNone, m.TypeFunction(void_id))};
    m.AddLabel(m.OpLabel());
    const Id index{m.OpBitcast(u32_id, m.OpLoad(s32_id, vertex_index))};
    const Id record{m.OpIMul(u32_id, index, u32c(emu.record_words))};
    const auto load_word = [&](u32 offset) {
        return m.OpLoad(u32_id, m.OpAccessChain(word_pointer, vertices, u32c(0),
                                                m.OpIAdd(u32_id, record, u32c(offset))));
    };
    const Id valid{m.OpINotEqual(bool_id, load_word(0), u32c(0))};
    const bool layer_outputs{profile.support_viewport_index_layer_non_geometry};
    const auto assign = [&](u32 line, u32 offset) {
        const IR::Attribute attribute{line};
        if (IR::IsGeneric(attribute)) {
            Vec4Output& output{generics[IR::GenericAttributeIndex(attribute)]};
            if (!Sirit::ValidId(output.components[0])) {
                output.components = {f32c(0.0f), f32c(0.0f), f32c(0.0f), f32c(0.0f)};
            }
            output.components[IR::GenericAttributeElement(attribute)] =
                m.OpBitcast(f32_id, load_word(offset));
            return;
        }
        switch (attribute) {
        case IR::Attribute::PositionX:
        case IR::Attribute::PositionY:
        case IR::Attribute::PositionZ:
        case IR::Attribute::PositionW:
            position_value.components[static_cast<u32>(attribute) -
                                      static_cast<u32>(IR::Attribute::PositionX)] =
                m.OpBitcast(f32_id, load_word(offset));
            break;
        case IR::Attribute::PointSize:
            point_size_value = m.OpBitcast(f32_id, load_word(offset));
            break;
        case IR::Attribute::Layer:
            if (layer_outputs) {
                layer_value = m.OpBitcast(s32_id, load_word(offset));
            }
            break;
        case IR::Attribute::ViewportIndex:
            if (layer_outputs) {
                viewport_value = m.OpBitcast(s32_id, load_word(offset));
            }
            break;
        default:
            break;
        }
    };
    const u32 vertex_lines{static_cast<u32>(mesh.vertex_lines.size())};
    for (u32 line = 0; line < vertex_lines; ++line) {
        assign(mesh.vertex_lines[line], 1 + line);
    }
    for (u32 line = 0; line < mesh.primitive_lines.size(); ++line) {
        assign(mesh.primitive_lines[line], 1 + vertex_lines + line);
    }
    auto& pos{position_value.components};
    if (runtime_info.convert_depth_mode && !profile.support_native_ndc) {
        pos[2] = m.OpFMul(f32_id, m.OpFAdd(f32_id, pos[2], pos[3]), f32c(0.5f));
    }
    // Invalid records land outside the clip volume.
    static constexpr std::array<f32, 4> culled{2.0f, 2.0f, 2.0f, 1.0f};
    for (size_t element = 0; element < 4; ++element) {
        pos[element] = m.OpSelect(f32_id, valid, pos[element], f32c(culled[element]));
    }
    m.OpStore(position, m.OpCompositeConstruct(vec4_id, pos));
    for (u32 location = 0; location < generics.size(); ++location) {
        Vec4Output& output{generics[location]};
        if (!Sirit::ValidId(output.components[0])) {
            continue;
        }
        output.variable = define(vec4_id, spv::StorageClass::Output, std::nullopt);
        m.Decorate(output.variable, spv::Decoration::Location, location);
        m.OpStore(output.variable, m.OpCompositeConstruct(vec4_id, output.components));
    }
    if (mesh.PrimitiveVertices() == 1) {
        const Id point_size{define(f32_id, spv::StorageClass::Output, spv::BuiltIn::PointSize)};
        m.OpStore(point_size, Sirit::ValidId(point_size_value)
                                  ? point_size_value
                                  : f32c(runtime_info.fixed_state_point_size.value_or(1.0f)));
    }
    if (Sirit::ValidId(layer_value) || Sirit::ValidId(viewport_value)) {
        m.AddExtension("SPV_EXT_shader_viewport_index_layer");
        m.AddCapability(spv::Capability::ShaderViewportIndexLayerEXT);
        if (Sirit::ValidId(layer_value)) {
            m.OpStore(define(s32_id, spv::StorageClass::Output, spv::BuiltIn::Layer), layer_value);
        }
        if (Sirit::ValidId(viewport_value)) {
            m.OpStore(define(s32_id, spv::StorageClass::Output, spv::BuiltIn::ViewportIndex),
                      viewport_value);
        }
    }
    m.OpReturn();
    m.OpFunctionEnd();
    m.AddEntryPoint(spv::ExecutionModel::Vertex, main, "main", interfaces);
    return m.Assemble();
}

} // namespace Shader::Backend::SPIRV
