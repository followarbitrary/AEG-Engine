#include "RendererImpl.hpp"

namespace aeg::render {
void Renderer::Impl::uploadModel(const ModelData& source) {
    if (source.meshes.empty()) throw std::runtime_error("Model has no renderable meshes");
    device.waitIdle();
    // Build a complete replacement first. Allocation errors preserve the old model.
    auto replacement = std::make_unique<GpuModel>();
    const auto materialCount = std::max<std::size_t>(1, source.materials.size());
    if (materialCount > std::numeric_limits<std::uint32_t>::max() / 2) throw std::runtime_error("Too many materials");
    replacement->materials.reserve(materialCount);
    const MaterialData fallback{};
    const assets::ImageData neutralNormal{1, 1, {128, 128, 255, 255}};
    const auto uploadTexture = [&](const assets::ImageData& image, rhi::Format format) {
        const auto maximum = device.capabilities().maximumImageDimension;
        const auto bytes = static_cast<std::uint64_t>(image.width) * image.height * 4;
        if (!image.width || !image.height || image.width > maximum || image.height > maximum ||
            bytes != image.rgba.size())
            throw std::runtime_error("Invalid or oversized material texture");
        return device.uploadTexture(
            {{image.width, image.height}, format, rhi::TextureUsage::CopyDestination | rhi::TextureUsage::Sampled, 1},
            std::as_bytes(std::span(image.rgba)));
    };
    for (std::size_t i = 0; i < materialCount; ++i) {
        const auto& material = source.materials.empty() ? fallback : source.materials[i];
        GpuMaterial gpu;
        if (!std::isfinite(material.normalScale)) throw std::runtime_error("Invalid normal texture scale");
        gpu.image = uploadTexture(material.texture, rhi::Format::Rgba8Srgb);
        gpu.normalImage =
            uploadTexture(material.hasNormalMap ? material.normalTexture : neutralNormal, rhi::Format::Rgba8Unorm);
        gpu.parameters.color = material.baseColor;
        gpu.parameters.alphaMasked = material.alphaMasked ? 1u : 0u;
        gpu.parameters.alphaCutoff = material.alphaCutoff;
        gpu.parameters.normalScale = material.normalScale;
        gpu.parameters.hasNormalMap = material.hasNormalMap ? 1u : 0u;
        const std::array writes{rhi::BindingWrite{0, nullptr, gpu.image.get(), sampler.get()},
                                rhi::BindingWrite{1, nullptr, gpu.normalImage.get(), sampler.get()}};
        gpu.descriptor = device.createBindingSet(*materialLayout, writes);
        replacement->materials.push_back(std::move(gpu));
    }
    replacement->meshes.reserve(source.meshes.size());
    for (const auto& mesh : source.meshes) {
        if (mesh.vertices.empty() || mesh.indices.empty() ||
            mesh.indices.size() > std::numeric_limits<std::uint32_t>::max())
            throw std::runtime_error("Invalid or oversized mesh");
        if (mesh.materialIndex >= materialCount || mesh.indices.size() % 3 ||
            std::ranges::any_of(mesh.indices, [&](auto index) { return index >= mesh.vertices.size(); }))
            throw std::runtime_error("Invalid mesh indices or material");
        GpuMesh gpu;
        gpu.vertices = device.uploadBuffer(std::as_bytes(std::span(mesh.vertices)), rhi::BufferUsage::Vertex);
        gpu.indices = device.uploadBuffer(std::as_bytes(std::span(mesh.indices)), rhi::BufferUsage::Index);
        gpu.indexCount = static_cast<std::uint32_t>(mesh.indices.size());
        gpu.material = mesh.materialIndex;
        replacement->meshes.push_back(std::move(gpu));
    }
    model = std::move(replacement);
    resetTemporalHistory();
}
}
