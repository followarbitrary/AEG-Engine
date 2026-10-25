#include <ModelData.hpp>

namespace aeg::assets {
std::size_t ModelData::vertexCount() const {
    std::size_t count = 0;
    for (const auto& mesh : meshes) count += mesh.vertices.size();
    return count;
}
std::size_t ModelData::triangleCount() const {
    std::size_t count = 0;
    for (const auto& mesh : meshes) count += mesh.indices.size() / 3;
    return count;
}
}
