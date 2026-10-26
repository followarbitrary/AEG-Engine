#pragma once
#include <RhiPipeline.hpp>
#include <memory>
namespace aeg::render {
struct ScenePipeline {
    std::uint32_t samples{1};
    bool temporal{};
    std::unique_ptr<rhi::RenderPass> pass;
    std::unique_ptr<rhi::GraphicsPipeline> pipeline;
};
}
