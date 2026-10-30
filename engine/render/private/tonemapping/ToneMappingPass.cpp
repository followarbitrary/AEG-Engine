#include "RendererImpl.hpp"

namespace aeg::render {
namespace {
struct alignas(16) TonePush {
    std::uint32_t mapper;
    float exposureEV;
    std::array<float, 2> padding{};
};
static_assert(sizeof(TonePush) == 16 && offsetof(TonePush, mapper) == 0 && offsetof(TonePush, exposureEV) == 4);
static_assert(std::is_standard_layout_v<TonePush> && std::is_trivially_copyable_v<TonePush>);
}

void Renderer::Impl::createToneMappingPass() {
    toneLayout = device.createPipelineLayout({{postSetLayout.get()}, sizeof(TonePush), rhi::ShaderStage::Fragment});
    const rhi::AttachmentDesc color{displayFormat,
                                    1,
                                    rhi::LoadOp::Discard,
                                    rhi::StoreOp::Store,
                                    rhi::ResourceState::ColorAttachment,
                                    rhi::ResourceState::ColorAttachment};
    tonePass = device.createRenderPass({{color}, {0}, {}, {}});
    tonePipeline = createPipeline(*tonePass, *toneLayout, 1, false, detail::ShaderId::FullscreenVertex,
                                  detail::ShaderId::ToneMappingFragment);
}

void Renderer::Impl::setToneMapping(const ToneMappingSettings& requested) {
    if ((requested.mapper != ToneMapper::Linear && requested.mapper != ToneMapper::Aces &&
         requested.mapper != ToneMapper::Agx) ||
        !std::isfinite(requested.exposureEV) || requested.exposureEV < -12 || requested.exposureEV > 12)
        throw std::invalid_argument("Tone mapping requires a known mapper and finite exposure in [-12,+12] EV");
    if (requested == toneMappingSettings) return;
    // History stores unexposed scene radiance; display changes preserve it and the last submitted image.
    toneMappingSettings = requested;
}

void Renderer::Impl::drawToneMapping(rhi::CommandList& commands) {
    const auto sourceIndex = aa.taa ? 1u + (1u - recordingHistory.readIndex) : 0u;
    commands.setViewport(viewport->extent());
    commands.bindPipeline(*tonePipeline);
    commands.bindSet(*toneLayout, 0, *viewport->toneDescriptors[sourceIndex]);
    const TonePush push{std::to_underlying(toneMappingSettings.mapper), toneMappingSettings.exposureEV};
    commands.pushConstants(*toneLayout, std::as_bytes(std::span(&push, 1)));
    commands.draw(3);
}
}
