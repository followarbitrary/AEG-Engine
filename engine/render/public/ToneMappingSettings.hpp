#pragma once
#include <cstdint>

namespace aeg::render {
enum class ToneMapper : std::uint32_t { Linear, Aces };
struct ToneMappingSettings {
    ToneMapper mapper{ToneMapper::Aces};
    float exposureEV{0}; // Exposure in stops, finite [-12,+12].
    bool operator==(const ToneMappingSettings&) const = default;
};
}
