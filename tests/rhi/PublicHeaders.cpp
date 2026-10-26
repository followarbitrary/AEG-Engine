#include <RhiTypes.hpp>
#include <RhiDevice.hpp>
#include <RhiBuffer.hpp>
#include <RhiTexture.hpp>
#include <RhiSampler.hpp>
#include <RhiShader.hpp>
#include <RhiBinding.hpp>
#include <RhiPipeline.hpp>
#include <RhiCommandList.hpp>
#include <RhiSwapchain.hpp>
#include <type_traits>

static_assert(std::has_virtual_destructor_v<aeg::rhi::Device>);
static_assert(std::has_virtual_destructor_v<aeg::rhi::Texture>);
static_assert(std::has_virtual_destructor_v<aeg::rhi::CommandList>);
static_assert(!std::is_copy_constructible_v<aeg::rhi::Device>);
