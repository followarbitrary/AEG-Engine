#pragma once
#include <EnvironmentImage.hpp>
#include <RhiDevice.hpp>
#include <memory>

namespace aeg::render {
struct EnvironmentBakeSettings {
    std::uint32_t environmentSize{256}, irradianceSize{32}, prefilterSize{128}, brdfSize{128}, sampleCount{256};
};
struct EnvironmentMaps {
    std::unique_ptr<rhi::Texture> environment, irradiance, prefiltered;
    std::uint32_t prefilterMipCount{};
};
class EnvironmentBaker {
public:
    explicit EnvironmentBaker(rhi::Device& device, const EnvironmentBakeSettings& settings = {});
    ~EnvironmentBaker();
    EnvironmentBaker(const EnvironmentBaker&) = delete;
    EnvironmentBaker& operator=(const EnvironmentBaker&) = delete;
    [[nodiscard]] EnvironmentMaps bake(const assets::HdrImageData& image);
    [[nodiscard]] rhi::Texture& brdfLut() const;
    [[nodiscard]] static bool supported(rhi::Device& device);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}
