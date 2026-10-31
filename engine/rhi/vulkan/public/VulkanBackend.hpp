#pragma once
#include <RhiDevice.hpp>
#include <RhiSwapchain.hpp>
namespace aeg::rhi::vulkan {
[[nodiscard]] std::unique_ptr<Device> createVulkanDevice(const DeviceDesc& desc);
[[nodiscard]] std::unique_ptr<Swapchain> createVulkanSwapchain(Device& device, std::uint32_t framesInFlight = 2);
}
