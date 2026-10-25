#pragma once
#include <cstdint>

namespace aeg::render {
struct AntialiasingSettings {
    std::uint32_t msaaSamples{1};
    bool fxaa{false};
    bool taa{false};
    bool operator==(const AntialiasingSettings&) const = default;
};
}
