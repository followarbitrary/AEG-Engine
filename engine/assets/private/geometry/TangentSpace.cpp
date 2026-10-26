#include "TangentSpace.hpp"
#include <array>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <vector>

namespace aeg::assets::detail {
namespace {
bool finite(const glm::dvec3& value) {
    return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
}
struct Accumulation {
    glm::dvec3 sum{};
    glm::dvec3 first{};
    float sign{};
};
}

bool buildTangentSpace(MeshData& mesh) {
    constexpr auto unused = std::numeric_limits<std::uint32_t>::max();
    const auto originalCount = mesh.vertices.size();
    std::vector<std::array<std::uint32_t, 3>> variants(originalCount, {unused, unused, unused});
    std::vector<Accumulation> accumulated(originalCount);
    for (auto& vertex : mesh.vertices) vertex.tangent = glm::vec4(0);
    bool allUsable = true;
    for (std::size_t offset = 0; offset + 2 < mesh.indices.size(); offset += 3) {
        const std::array original{mesh.indices[offset], mesh.indices[offset + 1], mesh.indices[offset + 2]};
        const auto& a = mesh.vertices[original[0]];
        const auto& b = mesh.vertices[original[1]];
        const auto& c = mesh.vertices[original[2]];
        const auto edge1 = glm::dvec3(b.position) - glm::dvec3(a.position);
        const auto edge2 = glm::dvec3(c.position) - glm::dvec3(a.position);
        const auto uv1 = glm::dvec2(b.uv) - glm::dvec2(a.uv);
        const auto uv2 = glm::dvec2(c.uv) - glm::dvec2(a.uv);
        const auto determinant = uv1.x * uv2.y - uv1.y * uv2.x;
        const auto uvMagnitude = glm::length(uv1) * glm::length(uv2);
        const auto area = glm::length(glm::cross(edge1, edge2));
        bool usable = uvMagnitude > 0 && std::isfinite(determinant) && std::abs(determinant) > 1e-10 * uvMagnitude &&
                      area > 0 && std::isfinite(area);
        glm::dvec3 tangent{}, bitangent{};
        if (usable) {
            tangent = (edge1 * uv2.y - edge2 * uv1.y) / determinant;
            bitangent = (edge2 * uv1.x - edge1 * uv2.x) / determinant;
            usable = finite(tangent) && finite(bitangent);
        }
        std::array<glm::dvec3, 3> directions{};
        std::array<float, 3> signs{};
        for (std::size_t corner = 0; usable && corner < 3; ++corner) {
            const auto normal = glm::normalize(glm::dvec3(mesh.vertices[original[corner]].normal));
            const auto projected = tangent - normal * glm::dot(normal, tangent);
            const auto length = glm::length(projected);
            const auto bitangentLength = glm::length(bitangent);
            if (!(length > 0) || !std::isfinite(length) || !(bitangentLength > 0)) {
                usable = false;
                break;
            }
            directions[corner] = projected / length;
            const auto orientation = glm::dot(glm::cross(normal, directions[corner]), bitangent);
            if (!std::isfinite(orientation) || std::abs(orientation) <= 1e-10 * bitangentLength) {
                usable = false;
                break;
            }
            signs[corner] = orientation < 0 ? -1.0f : 1.0f;
        }
        if (!usable) allUsable = false;
        for (std::size_t corner = 0; corner < 3; ++corner) {
            const auto sourceIndex = original[corner];
            const auto sign = usable ? signs[corner] : 0.0f;
            const std::size_t group = sign < 0 ? 0 : (sign > 0 ? 2 : 1);
            auto& slots = variants[sourceIndex];
            auto& target = slots[group];
            if (target == unused) {
                if (slots[0] == unused && slots[1] == unused && slots[2] == unused) {
                    target = sourceIndex;
                } else {
                    if (mesh.vertices.size() >= unused)
                        throw std::runtime_error("Normal-mapped mesh exceeds supported vertex count");
                    target = static_cast<std::uint32_t>(mesh.vertices.size());
                    // Copy first: vector growth may otherwise invalidate its source reference.
                    const auto vertex = mesh.vertices[sourceIndex];
                    mesh.vertices.push_back(vertex);
                    accumulated.emplace_back();
                }
                accumulated[target].sign = sign;
                if (usable) accumulated[target].first = directions[corner];
            }
            mesh.indices[offset + corner] = target;
            if (usable) accumulated[target].sum += directions[corner] * area;
        }
    }
    for (std::size_t index = 0; index < mesh.vertices.size(); ++index) {
        auto& vertex = mesh.vertices[index];
        const auto& value = accumulated[index];
        if (value.sign == 0) continue;
        const auto normal = glm::normalize(glm::dvec3(vertex.normal));
        auto tangent = value.sum - normal * glm::dot(normal, value.sum);
        auto length = glm::length(tangent);
        // Equal opposing directions can cancel at a seam. Preserve a valid
        // contributing frame rather than normalizing a zero sum.
        if (!(length > 0) || !std::isfinite(length)) {
            tangent = value.first;
            length = glm::length(tangent);
        }
        vertex.tangent = glm::vec4(glm::vec3(tangent / length), value.sign);
    }
    return allUsable;
}
}
