#pragma once

#include "MMDPmxParser.h"
#include <assimp/Exceptional.h>
#include <limits>
#include <vector>

namespace aeg_assimp_patch {
// Validate the parsed inputs that Assimp 5.4.3 dereferences while constructing
// its scene. This does not replace bounds checking in the binary PMX parser.
inline void validateCount(int count, const void* data, const char* name) {
    if (count < 0 || static_cast<unsigned long long>(count) > (std::numeric_limits<unsigned int>::max)() ||
        (count > 0 && data == nullptr))
        throw ::DeadlyImportError("MMD: Invalid ", name, " count or storage.");
}

inline void validate(const pmx::PmxModel& model) {
    validateCount(model.vertex_count, model.vertices.get(), "vertex");
    validateCount(model.index_count, model.indices.get(), "index");
    validateCount(model.material_count, model.materials.get(), "material");
    validateCount(model.texture_count, model.textures.get(), "texture");
    validateCount(model.bone_count, model.bones.get(), "bone");
    if (model.vertex_count == 0 || model.index_count == 0 || model.material_count == 0)
        throw ::DeadlyImportError("MMD: Model contains no materialized triangles.");
    if (model.index_count % 3 != 0) throw ::DeadlyImportError("MMD: Index count is not a triangle list.");
    if (model.setting.uv > 4) throw ::DeadlyImportError("MMD: More than four additional UV channels are invalid.");

    for (int i = 0; i < model.index_count; ++i) {
        if (model.indices[i] < 0 || model.indices[i] >= model.vertex_count)
            throw ::DeadlyImportError("MMD: Vertex index is out of range.");
    }

    // Subtract from the remaining count instead of summing untrusted spans.
    int remaining = model.index_count;
    for (int i = 0; i < model.material_count; ++i) {
        const auto& material = model.materials[i];
        if (material.index_count < 0 || material.index_count % 3 != 0 || material.index_count > remaining)
            throw ::DeadlyImportError("MMD: Material index span is invalid.");
        remaining -= material.index_count;
        if (material.diffuse_texture_index < -1 || material.diffuse_texture_index >= model.texture_count)
            throw ::DeadlyImportError("MMD: Diffuse texture index is out of range.");
    }
    if (remaining != 0) throw ::DeadlyImportError("MMD: Material index spans do not cover the triangle list.");

    for (int i = 0; i < model.bone_count; ++i) {
        const int parent = model.bones[i].parent_index;
        if (parent < -1 || parent >= model.bone_count || parent == i)
            throw ::DeadlyImportError("MMD: Bone parent index is invalid.");
    }

    // Each node has at most one parent. Finish each chain before visiting the
    // next: encountering a visiting node then proves a cycle in that chain.
    std::vector<unsigned char> colors(static_cast<std::size_t>(model.bone_count), 0);
    for (int start = 0; start < model.bone_count; ++start) {
        if (colors[start] != 0) continue;
        int current = start;
        while (current != -1 && colors[current] == 0) {
            colors[current] = 1;
            current = model.bones[current].parent_index;
        }
        if (current != -1 && colors[current] == 1)
            throw ::DeadlyImportError("MMD: Bone parent hierarchy contains a cycle.");
        current = start;
        while (current != -1 && colors[current] == 1) {
            colors[current] = 2;
            current = model.bones[current].parent_index;
        }
    }
}
}
