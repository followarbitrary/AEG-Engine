#include "RendererImpl.hpp"

namespace aeg::render {
namespace {
using Corners = std::array<math::DVec3, 8>;

bool transformedCorners(const GpuModel& model, const math::Mat4& transform, Corners& corners) {
    if (!math::is_finite(transform) || transform.at(3, 0) != 0 || transform.at(3, 1) != 0 || transform.at(3, 2) != 0 ||
        transform.at(3, 3) != 1)
        return false;
    for (std::size_t i = 0; i < corners.size(); ++i) {
        const math::DVec3 local{(i & 1) ? model.maximum.x : model.minimum.x,
                                (i & 2) ? model.maximum.y : model.minimum.y,
                                (i & 4) ? model.maximum.z : model.minimum.z};
        for (std::size_t row = 0; row < 3; ++row) {
            double value = transform.at(row, 3);
            for (std::size_t column = 0; column < 3; ++column)
                value += static_cast<double>(transform.at(row, column)) * local[column];
            if (!std::isfinite(value) || std::abs(value) > std::numeric_limits<float>::max()) return false;
            corners[i][row] = value;
        }
    }
    return true;
}

struct LightBasis {
    math::DVec3 forward, right, up;
};
bool lightBasis(math::Vec3 direction, LightBasis& basis) {
    if (!math::try_normalize(math::DVec3(direction), basis.forward)) return false;
    // Choosing an axis away from the direction avoids the vertical-light singularity.
    const math::DVec3 reference = std::abs(basis.forward.y) < 0.9 ? math::DVec3{0, 1, 0} : math::DVec3{1, 0, 0};
    if (!math::try_normalize(math::cross(basis.forward, reference), basis.right)) return false;
    basis.up = math::cross(basis.right, basis.forward);
    return math::is_finite(basis.up);
}

bool usableMatrix(const math::Mat4& matrix) {
    math::Mat4 inverse;
    return math::is_finite(matrix) && math::try_inverse(matrix, inverse, 0);
}

bool directionalProjection(const Corners& corners, const LightBasis& basis, std::uint32_t resolution,
                           math::Mat4& output) {
    math::DVec3 minimum(std::numeric_limits<double>::max()), maximum(std::numeric_limits<double>::lowest());
    for (const auto& corner : corners) {
        const math::DVec3 projected{math::dot(basis.right, corner), math::dot(basis.up, corner),
                                    math::dot(basis.forward, corner)};
        minimum = math::min(minimum, projected);
        maximum = math::max(maximum, projected);
    }
    const auto span = maximum - minimum;
    const double scale = std::max({span.x, span.y, span.z});
    const double nonzeroScale = scale > 0 ? scale : 1.0;
    const double floatError = 8.0 * std::numeric_limits<float>::epsilon();
    const double widthError = std::max(std::abs(minimum.x), std::abs(maximum.x)) * floatError;
    const double heightError = std::max(std::abs(minimum.y), std::abs(maximum.y)) * floatError;
    // Two texels of padding on each side cover the 3x3 footprint and the half
    // texel by which snapping can move the center. The near/far box is padded too.
    const double mapSize = static_cast<double>(resolution);
    const double width = (std::max(span.x, nonzeroScale * 1e-4) + 2 * widthError) * mapSize / (mapSize - 4);
    const double height = (std::max(span.y, nonzeroScale * 1e-4) + 2 * heightError) * mapSize / (mapSize - 4);
    const double texelX = width / mapSize, texelY = height / mapSize;
    if (!(texelX > 0) || !(texelY > 0) || !std::isfinite(width) || !std::isfinite(height)) return false;
    const double centerX = std::floor(((minimum.x + maximum.x) * 0.5) / texelX + 0.5) * texelX;
    const double centerY = std::floor(((minimum.y + maximum.y) * 0.5) / texelY + 0.5) * texelY;
    const double depthPadding =
        std::max(nonzeroScale * 0.02, std::max(std::abs(minimum.z), std::abs(maximum.z)) * floatError);
    const double firstDepth = minimum.z - depthPadding;
    const double depthRange = span.z + 2 * depthPadding;
    if (!(depthRange > 0) || !std::isfinite(depthRange)) return false;
    // This is P*V for a right-handed light view with the eye behind the nearest
    // corner, a positive near distance, y-down, and non-reversed [0,1] depth.
    // Forming its rows in double avoids an intermediate far-away float eye.
    auto matrix = math::identity();
    for (std::size_t axis = 0; axis < 3; ++axis) {
        matrix.at(0, axis) = static_cast<float>(2 * basis.right[axis] / width);
        matrix.at(1, axis) = static_cast<float>(-2 * basis.up[axis] / height);
        matrix.at(2, axis) = static_cast<float>(basis.forward[axis] / depthRange);
    }
    matrix.at(0, 3) = static_cast<float>(-2 * centerX / width);
    matrix.at(1, 3) = static_cast<float>(2 * centerY / height);
    matrix.at(2, 3) = static_cast<float>(-firstDepth / depthRange);
    if (!usableMatrix(matrix)) return false;
    // Reject an unrepresentable fit instead of sending invalid shadow data to
    // the shader. Ordinary affine transforms, including reflections, fit all corners.
    for (const auto& corner : corners) {
        const auto clip = matrix * math::Vec4(math::Vec3(corner), 1);
        if (!math::is_finite(clip) || std::abs(clip.x) > 1.0001f || std::abs(clip.y) > 1.0001f || clip.z < 0 ||
            clip.z > 1)
            return false;
    }
    output = matrix;
    return true;
}

bool spotProjection(const Corners& corners, const LightBasis& basis, const GpuLight& light, math::Mat4& output) {
    const double farDistance = light.positionRange.w;
    const double fov = 2 * std::acos(std::clamp(static_cast<double>(light.cone.y), -1.0, 1.0));
    if (!(farDistance > 0) || !std::isfinite(farDistance) || !(fov > 0) || !(fov < math::kPi)) return false;
    const math::DVec3 position{math::Vec3(light.positionRange)};
    double closestDepth = std::numeric_limits<double>::max();
    for (const auto& corner : corners)
        closestDepth = std::min(closestDepth, math::dot(basis.forward, corner - position));
    // A fit entirely in front of the light lets the near plane follow its bounds.
    // Bounds crossing the light plane need a smaller, still positive near plane.
    const double nearDistance = closestDepth > 0 ? std::min(farDistance * 0.1, closestDepth * 0.5) : farDistance * 1e-4;
    if (!(nearDistance > 0) || !(nearDistance < farDistance)) return false;
    const double focal = 1 / std::tan(fov * 0.5);
    const double depthScale = farDistance / (farDistance - nearDistance);
    const double depthOffset = farDistance * nearDistance / (farDistance - nearDistance);
    math::Mat4 matrix;
    for (std::size_t axis = 0; axis < 3; ++axis) {
        matrix.at(0, axis) = static_cast<float>(focal * basis.right[axis]);
        matrix.at(1, axis) = static_cast<float>(-focal * basis.up[axis]);
        matrix.at(2, axis) = static_cast<float>(depthScale * basis.forward[axis]);
        matrix.at(3, axis) = static_cast<float>(basis.forward[axis]);
    }
    matrix.at(0, 3) = static_cast<float>(-focal * math::dot(basis.right, position));
    matrix.at(1, 3) = static_cast<float>(focal * math::dot(basis.up, position));
    matrix.at(2, 3) = static_cast<float>(-depthScale * math::dot(basis.forward, position) - depthOffset);
    matrix.at(3, 3) = static_cast<float>(-math::dot(basis.forward, position));
    if (!usableMatrix(matrix)) return false;
    output = matrix;
    return true;
}
}

void Renderer::Impl::initializeShadowSupport() {
    const auto& caps = device.capabilities();
    constexpr auto usage = rhi::TextureUsage::DepthAttachment | rhi::TextureUsage::Sampled;
    for (const auto format : {rhi::Format::D32Float, rhi::Format::D16Unorm, rhi::Format::D24UnormS8}) {
        const auto support = device.textureSupport(format, usage);
        if (!support.supported || !(support.sampleMask & 1u)) continue;
        const auto maximum = std::min({4096u, caps.maximumImageDimension, caps.maximumFramebufferDimension,
                                       support.maximumExtent.width, support.maximumExtent.height});
        if (maximum < 16) continue;
        shadowFormat = format;
        maximumShadow = std::bit_floor(maximum);
        shadowSettings.resolution = std::min(shadowSettings.resolution, maximumShadow);
        shadowSupported = true;
        break;
    }
}

void Renderer::Impl::setShadows(const ShadowSettings& requested) {
    if (requested.lightIndex >= kMaxLights || requested.resolution < 16 || requested.resolution > 4096 ||
        !std::has_single_bit(requested.resolution) || !std::isfinite(requested.depthBias) || requested.depthBias < 0 ||
        requested.depthBias > 1 || !std::isfinite(requested.slopeBias) || requested.slopeBias < 0 ||
        requested.slopeBias > 1)
        throw std::invalid_argument("Invalid shadow light, resolution or depth bias");
    if (requested.enabled && !shadowSupported) throw std::runtime_error("Sampled depth shadows are unsupported");
    if (shadowSupported && requested.resolution > maximumShadow)
        throw std::invalid_argument("Shadow resolution exceeds device capability");
    if (requested == shadowSettings) return;
    std::unique_ptr<ShadowResources> replacement;
    if (requested.enabled && (!shadow || shadow->resolution != requested.resolution)) {
        replacement = std::make_unique<ShadowResources>();
        replacement->resolution = requested.resolution;
        const rhi::Extent2D extent{requested.resolution, requested.resolution};
        replacement->texture = device.createTexture(
            {extent, shadowFormat, rhi::TextureUsage::DepthAttachment | rhi::TextureUsage::Sampled, 1});
        // Every recording clears from Undefined and ends Sampled. No CPU-side
        // initialization flag can accidentally advance on an abandoned frame.
        replacement->pass = device.createRenderPass({{{shadowFormat, 1, rhi::LoadOp::Clear, rhi::StoreOp::Store,
                                                       rhi::ResourceState::Undefined, rhi::ResourceState::Sampled}},
                                                     {},
                                                     0,
                                                     {}});
        replacement->framebuffer =
            device.createFramebuffer({replacement->pass.get(), {replacement->texture.get()}, extent});
        replacement->pipeline = createPipeline(*replacement->pass, *pipelineLayout, 1, true,
                                               detail::ShaderId::ShadowVertex, detail::ShaderId::ShadowFragment, 0);
        const std::array writes{rhi::BindingWrite{0, nullptr, replacement->texture.get(), pointSampler.get()}};
        replacement->descriptor = device.createBindingSet(*shadowSetLayout, writes);
        device.waitIdle();
    }
    // All candidate allocation, compilation, descriptor creation and waiting
    // succeeded. Publishing and resetting history below cannot throw.
    if (replacement) shadow.swap(replacement);
    shadowSettings = requested;
    resetTemporalHistory();
}

void Renderer::Impl::configureShadowUniform(SceneUniform& uniform, const RenderSettings&) {
    uniform.lightViewProjection = math::identity();
    uniform.shadowInfo = {0, static_cast<float>(shadowSettings.lightIndex),
                          1.0f / static_cast<float>(shadowSettings.resolution), 0};
    uniform.shadowBias = {shadowSettings.depthBias, shadowSettings.slopeBias, 0, 0};
    if (!shadowSettings.enabled || !shadow || !model ||
        static_cast<float>(shadowSettings.lightIndex) >= uniform.lightInfo.x)
        return;
    // Use exactly the sanitized record consumed by direct lighting.
    const auto& light = uniform.lights[shadowSettings.lightIndex];
    if (light.radiance.w < 0.5f || (light.directionType.w != static_cast<float>(LightType::Directional) &&
                                    light.directionType.w != static_cast<float>(LightType::Spot)))
        return;
    Corners corners;
    LightBasis basis;
    if (!transformedCorners(*model, uniform.model, corners) || !lightBasis(math::Vec3(light.directionType), basis))
        return;
    math::Mat4 projection;
    const bool fitted = light.directionType.w == static_cast<float>(LightType::Directional)
                            ? directionalProjection(corners, basis, shadow->resolution, projection)
                            : spotProjection(corners, basis, light, projection);
    if (!fitted) return;
    uniform.lightViewProjection = projection;
    uniform.shadowInfo.x = 1;
}

void Renderer::Impl::drawShadow(rhi::CommandList& commands, const SceneUniform& uniform) {
    if (uniform.shadowInfo.x < 0.5f || !shadow || !model) return;
    const rhi::Extent2D extent{shadow->resolution, shadow->resolution};
    const std::array<rhi::ClearValue, 1> clears{}; // Canonical non-reversed depth clears to one.
    commands.beginRenderPass(*shadow->pass, *shadow->framebuffer, extent, clears);
    commands.setViewport(extent);
    commands.bindPipeline(*shadow->pipeline);
    commands.bindSet(*pipelineLayout, 0, *sceneDescriptor);
    for (const auto& mesh : model->meshes) {
        const auto& material = model->materials[mesh.material];
        if (material.parameters.alphaMode == static_cast<std::uint32_t>(assets::AlphaMode::Blend)) continue;
        commands.bindVertexBuffer(*mesh.vertices);
        commands.bindIndexBuffer(*mesh.indices);
        commands.bindSet(*pipelineLayout, 1, *material.descriptor);
        commands.pushConstants(*pipelineLayout, std::as_bytes(std::span(&material.parameters, 1)));
        commands.drawIndexed(mesh.indexCount);
    }
    commands.endRenderPass();
}
}
