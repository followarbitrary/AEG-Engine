#pragma once
#include <RhiPipeline.hpp>
#include <memory>
namespace aeg::render {
struct ScenePipeline {
    std::uint32_t samples{1};
    bool temporal{};
    std::unique_ptr<rhi::RenderPass> pass;
    std::unique_ptr<rhi::GraphicsPipeline> less, greater;
    std::unique_ptr<rhi::GraphicsPipeline> bindlessLess, bindlessGreater;
    std::unique_ptr<rhi::GraphicsPipeline> blendLess, blendGreater;
};
}
