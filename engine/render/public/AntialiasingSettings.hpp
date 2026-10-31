#pragma once
#include <cstdint>

namespace aeg::render {
struct AntialiasingSettings {
    std::uint32_t msaaSamples{1};
    bool fxaa{false};
    bool taa{false};
    float taaHistoryWeight{0.9f};
    float taaDepthThreshold{0.01f};
    bool operator==(const AntialiasingSettings&) const = default;
};
}
