#include "RendererImpl.hpp"

namespace aeg::render {
void Renderer::Impl::uploadModel(const ModelData& source) {
    if (source.meshes.empty()) throw std::runtime_error("Model has no renderable meshes");
    device.waitIdle();
    // Build a complete replacement first. Allocation errors preserve the old model.
    auto replacement = std::make_unique<GpuModel>();
    replacement->minimum = math::Vec3(std::numeric_limits<float>::max());
    replacement->maximum = math::Vec3(std::numeric_limits<float>::lowest());
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
        if (!std::isfinite(material.metallicFactor) || material.metallicFactor < 0 || material.metallicFactor > 1 ||
            !std::isfinite(material.roughnessFactor) || material.roughnessFactor < 0 || material.roughnessFactor > 1)
            throw std::runtime_error("Invalid material metallic or roughness factor");
        if (!std::isfinite(material.normalScale)) throw std::runtime_error("Invalid normal texture scale");
        if ((material.alphaMode != assets::AlphaMode::Opaque && material.alphaMode != assets::AlphaMode::Mask &&
             material.alphaMode != assets::AlphaMode::Blend) ||
            !math::is_finite(material.baseColor) || material.baseColor.w < 0 || material.baseColor.w > 1 ||
            !std::isfinite(material.alphaCutoff) || material.alphaCutoff < 0)
            throw std::runtime_error("Invalid material alpha mode, opacity or cutoff");
        gpu.image = uploadTexture(material.texture, rhi::Format::Rgba8Srgb);
        gpu.normalImage =
            uploadTexture(material.hasNormalMap ? material.normalTexture : neutralNormal, rhi::Format::Rgba8Unorm);
        gpu.parameters.color = material.baseColor;
        gpu.parameters.alphaMode = static_cast<std::uint32_t>(material.alphaMode);
        gpu.parameters.alphaCutoff = material.alphaCutoff;
        gpu.parameters.normalScale = material.normalScale;
        gpu.parameters.hasNormalMap = material.hasNormalMap ? 1u : 0u;
        gpu.parameters.pbrFactors = {material.metallicFactor, material.roughnessFactor, 0, 0};
        const std::array writes{rhi::BindingWrite{0, nullptr, gpu.image.get(), sampler.get()},
                                rhi::BindingWrite{1, nullptr, gpu.normalImage.get(), sampler.get()}};
        gpu.descriptor = device.createBindingSet(*materialLayout, writes);
        replacement->materials.push_back(std::move(gpu));
    }
    const bool bindlessMaterialCapacity = bindlessSupported && materialCount <= kBindlessTextureCapacity;
    if (bindlessMaterialCapacity) {
        std::vector<rhi::BindingWrite> writes;
        writes.reserve(2 * kBindlessTextureCapacity);
        for (std::uint32_t i = 0; i < kBindlessTextureCapacity; ++i) {
            const auto base = i < replacement->materials.size() ? replacement->materials[i].image.get()
                                                                  : bindlessFallbackBase.get();
            const auto normal = i < replacement->materials.size() ? replacement->materials[i].normalImage.get()
                                                                    : bindlessFallbackNormal.get();
            writes.push_back({0, nullptr, base, sampler.get(), 0, i});
            writes.push_back({1, nullptr, normal, sampler.get(), 0, i});
        }
        replacement->bindlessDescriptor = device.createBindingSet(*bindlessMaterialLayout, writes);
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
        math::Vec3 minimum(std::numeric_limits<float>::max()), maximum(std::numeric_limits<float>::lowest());
        for (const auto& vertex : mesh.vertices) {
            if (!math::is_finite(vertex.position)) throw std::runtime_error("Invalid mesh position");
            minimum = math::min(minimum, vertex.position);
            maximum = math::max(maximum, vertex.position);
        }
        gpu.center = {static_cast<float>((static_cast<double>(minimum.x) + maximum.x) * 0.5),
                      static_cast<float>((static_cast<double>(minimum.y) + maximum.y) * 0.5),
                      static_cast<float>((static_cast<double>(minimum.z) + maximum.z) * 0.5)};
        gpu.minimum = minimum;
        gpu.maximum = maximum;
        replacement->minimum = math::min(replacement->minimum, minimum);
        replacement->maximum = math::max(replacement->maximum, maximum);
        gpu.vertices = device.uploadBuffer(std::as_bytes(std::span(mesh.vertices)), rhi::BufferUsage::Vertex);
        gpu.indices = device.uploadBuffer(std::as_bytes(std::span(mesh.indices)), rhi::BufferUsage::Index);
        gpu.indexCount = static_cast<std::uint32_t>(mesh.indices.size());
        gpu.material = mesh.materialIndex;
        replacement->meshes.push_back(std::move(gpu));
    }
    const auto meshCount = replacement->meshes.size();
    const auto cullGroups = (meshCount + 63) / 64;
    const bool gpuCullCapacity = bindlessMaterialCapacity && meshCount &&
                                 cullGroups <= device.capabilities().maximumComputeGroupCount[0] &&
                                 meshCount <= std::numeric_limits<std::uint32_t>::max() / 5;
    if (gpuCullCapacity) {
        std::vector<GpuCullBounds> bounds(meshCount);
        for (std::size_t i = 0; i < meshCount; ++i) {
            const auto& mesh = replacement->meshes[i];
            bounds[i].localMinimum = {mesh.minimum, 0};
            bounds[i].localMaximum = {mesh.maximum, 0};
            bounds[i].draw = {mesh.indexCount, 0, 0, mesh.material};
        }
        replacement->boundsBuffer = device.uploadBuffer(std::as_bytes(std::span(bounds)), rhi::BufferUsage::Storage);
        const auto commandBytes = meshCount * sizeof(std::uint32_t) * 5;
        replacement->indirectBuffer = device.createBuffer(
            {commandBytes, rhi::BufferUsage::Storage | rhi::BufferUsage::Indirect | rhi::BufferUsage::CopySource,
             rhi::MemoryUsage::Device});
        const std::array writes{rhi::BindingWrite{0, replacement->boundsBuffer.get(), nullptr, nullptr},
                                rhi::BindingWrite{1, replacement->indirectBuffer.get(), nullptr, nullptr}};
        replacement->cullDescriptor = device.createBindingSet(*cullLayout, writes);
        replacement->gpuDriven = true;
    }
    if (std::ranges::any_of(replacement->materials, [](const auto& material) {
            return material.parameters.alphaMode == static_cast<std::uint32_t>(assets::AlphaMode::Blend);
        })) {
        // Pipeline validation/compilation is part of the replacement transaction,
        // so a failed transparent upload cannot discard the displayed model.
        scenePipeline(aa.msaaSamples, aa.taa, false, true);
        scenePipeline(aa.msaaSamples, aa.taa, true, true);
    }
    model = std::move(replacement);
    resetTemporalHistory();
}
}
