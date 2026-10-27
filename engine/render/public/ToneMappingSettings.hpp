#pragma once
#include <cstdint>

namespace aeg::render {
// Numeric values are part of the fragment push-constant ABI.
enum class ToneMapper : std::uint32_t { Linear = 0, Aces = 1, Agx = 2 };
struct ToneMappingSettings {
    ToneMapper mapper{ToneMapper::Aces};
    float exposureEV{0}; // Exposure in stops, finite [-12,+12].
    bool operator==(const ToneMappingSettings&) const = default;
};
}
