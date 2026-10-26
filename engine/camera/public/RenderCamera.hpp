#pragma once
#include <math.hpp>

namespace aeg::camera {
struct RenderCamera {
    math::Mat4 view{math::identity()};
    math::Mat4 projection{math::identity()};
    math::ClipSpaceConvention convention{};

    [[nodiscard]] math::Mat4 view_projection() const { return projection * view; }
};
}
