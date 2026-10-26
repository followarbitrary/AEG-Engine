#pragma once

namespace aeg::render {
struct EnvironmentSettings {
    bool enabled{false};
    float intensity{1};
    float rotationRadians{0}; // Right-handed rotation of the environment around world +Y.
    bool operator==(const EnvironmentSettings&) const = default;
};
}
