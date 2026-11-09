// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

// SPDX-FileCopyrightText: Copyright 2021 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <array>
#include <bitset>
#include <map>
#include <optional>
#include <vector>

#include "common/common_types.h"
#include "shader_recompiler/varying_state.h"

namespace Shader {

enum class AttributeType : u8 {
    Float,
    SignedInt,
    UnsignedInt,
    SignedScaled,
    UnsignedScaled,
    Disabled,
};

enum class InputTopology {
    Points,
    Lines,
    LinesAdjacency,
    Triangles,
    TrianglesAdjacency,
};

struct InputTopologyVertices {
    static u32 vertices(InputTopology input_topology) {
        switch (input_topology) {
        case InputTopology::Lines:
            return 2;
        case InputTopology::LinesAdjacency:
            return 4;
        case InputTopology::Triangles:
            return 3;
        case InputTopology::TrianglesAdjacency:
            return 6;
        case InputTopology::Points:
        default:
            return 1;
        }
    }
};

enum class CompareFunction {
    Never,
    Less,
    Equal,
    LessThanEqual,
    Greater,
    NotEqual,
    GreaterThanEqual,
    Always,
};

enum class TessPrimitive {
    Isolines,
    Triangles,
    Quads,
};

enum class TessSpacing {
    Equal,
    FractionalOdd,
    FractionalEven,
};

struct TransformFeedbackVarying {
    u32 buffer{};
    u32 stream{};
    u32 stride{};
    u32 offset{};
    u32 components{};
};

// Task and mesh programs as compute shaders, for devices without VK_EXT_mesh_shader. Their
// buffers sit in descriptor set MESH_EMU_SET:
// - args: the mesh dispatch (x, y, z) then the draw (vertex count, instances, first vertex,
//   first instance), both filled by the task workgroups
// - list: two words per mesh workgroup, its task workgroup and its index among that task's
// - payload: each task workgroup's payload, payload_words apart
// - vertices: record_words per output vertex (a valid flag, the vertex lines, the primitive
//   lines), group_vertices per mesh workgroup; a vertex shader draws them as a list.
constexpr u32 MESH_EMU_SET = 1;
constexpr u32 MESH_EMU_ARGS = 0;
constexpr u32 MESH_EMU_LIST = 1;
constexpr u32 MESH_EMU_PAYLOAD = 2;
constexpr u32 MESH_EMU_VERTICES = 3;
constexpr u32 MESH_EMU_NUM_BUFFERS = 4;
constexpr u32 MESH_EMU_ARGS_DRAW_WORD = 3;

struct MeshEmulation {
    bool enabled{};
    bool has_task{};          ///< the mesh workgroups come from a task program
    u32 primitive_vertices{}; ///< 1, 2 or 3
    u32 group_vertices{};     ///< output vertices per mesh workgroup
    u32 record_words{};       ///< words per output vertex
    u32 max_groups{};         ///< mesh workgroups the vertex and list buffers hold
    u32 payload_words{};      ///< payload words per task workgroup
    u32 max_task_groups{};    ///< task workgroups the payload buffer holds
};

struct RuntimeInfo {
    MeshEmulation mesh_emulation{};
    std::array<AttributeType, 32> generic_input_types{};
    VaryingState previous_stage_stores;
    /// Fragment stage after a mesh program: generic inputs it writes per primitive.
    std::bitset<32> per_primitive_generics{};
    std::map<IR::Attribute, IR::Attribute> previous_stage_legacy_stores_mapping;

    bool convert_depth_mode{};
    bool force_early_z{};

    TessPrimitive tess_primitive{};
    TessSpacing tess_spacing{};
    bool tess_clockwise{};

    InputTopology input_topology{};

    std::optional<float> fixed_state_point_size;
    std::optional<CompareFunction> alpha_test_func;
    float alpha_test_reference{};

    /// Static Y negate value
    bool y_negate{};
    /// Use storage buffers instead of global pointers on GLASM
    bool glasm_use_storage_buffers{};

    /// Transform feedback state for each varying
    std::array<TransformFeedbackVarying, 256> xfb_varyings{};
    u32 xfb_count{0};

    /// Output types for each color attachment
    std::array<AttributeType, 8> color_output_types{};

    /// Dual source blending
    bool dual_source_blend{};
};

} // namespace Shader
