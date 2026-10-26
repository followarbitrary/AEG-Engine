#pragma once
#include <RenderSettings.hpp>
#include <ShadowSettings.hpp>
#include "scene/SceneUniform.hpp"
#include <array>

namespace aeg::render {
struct ShadowFit {
    std::array<math::Mat4, kMaxShadowCascades> matrices{math::identity(), math::identity(), math::identity(),
                                                        math::identity()};
    math::Vec4 splits{};      // Positive camera-view far distance of each active cascade.
    math::Vec4 blendStarts{}; // Current/next blend starts; the last active entry is distance-fade start.
    std::uint32_t count{};
};

// Inputs are validated local vertex bounds and the same sanitized light sent to the shaders.
// Failure preserves out. Spot and cascadeCount=1 use legacy whole-model single-map fitting.
bool tryFitShadow(const RenderSettings& settings, const ShadowSettings& shadows, math::Vec3 minimum, math::Vec3 maximum,
                  const GpuLight& light, ShadowFit& out);
}
