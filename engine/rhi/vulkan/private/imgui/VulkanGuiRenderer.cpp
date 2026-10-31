#include <VulkanGuiBackend.hpp>
#include "../VulkanInterop.hpp"
#include <imgui_impl_vulkan.h>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <vector>

namespace aeg::rhi::vulkan {
namespace {
void checkGui(VkResult result) {
    if (result != VK_SUCCESS) throw std::runtime_error("ImGui Vulkan backend failed: " + std::to_string(result));
}

class VulkanGuiRenderer final : public gui::GuiRenderer {
public:
    VulkanGuiRenderer(Device& device, Swapchain& swapchain)
        : device_(device), swapchain_(swapchain), native_(detail::nativeContext(device)) {}
    ~VulkanGuiRenderer() override {
        if (initialized_) {
            for (const auto& [set, owner] : textures_) {
                (void)owner;
                ImGui_ImplVulkan_RemoveTexture(set);
            }
            for (const auto& retired : retiredTextures_) ImGui_ImplVulkan_RemoveTexture(retired.set);
            ImGui_ImplVulkan_Shutdown();
        }
        if (pool_) vkDestroyDescriptorPool(native_.device, pool_, nullptr);
    }
    void initialize() {
        if (!ImGui::GetCurrentContext() || ImGui::GetIO().BackendRendererUserData)
            throw std::runtime_error("Vulkan GUI requires a current, unused ImGui context");
        const VkDescriptorPoolSize size{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1024};
        VkDescriptorPoolCreateInfo pool{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
        pool.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
        pool.maxSets = 1024;
        pool.poolSizeCount = 1;
        pool.pPoolSizes = &size;
        checkGui(vkCreateDescriptorPool(native_.device, &pool, nullptr, &pool_));
        retiredTextures_.reserve(pool.maxSets);
        ImGui_ImplVulkan_InitInfo info{};
        info.ApiVersion = VK_API_VERSION_1_2;
        info.Instance = native_.instance;
        info.PhysicalDevice = native_.physical;
        info.Device = native_.device;
        info.QueueFamily = native_.graphicsFamily;
        info.Queue = native_.graphicsQueue;
        info.DescriptorPool = pool_;
        // ImGui is recorded after the graph clear pass and therefore needs the
        // swapchain's load-compatible render pass/pipeline compatibility key.
        info.RenderPass = detail::nativeGuiRenderPass(device_, swapchain_);
        info.MinImageCount = minimumImageCount_ = swapchain_.minimumImageCount();
        info.ImageCount = swapchain_.imageCount();
        info.MSAASamples = VK_SAMPLE_COUNT_1_BIT;
        info.CheckVkResultFn = checkGui;
        try {
            if (!ImGui_ImplVulkan_Init(&info)) throw std::runtime_error("ImGui Vulkan initialization failed");
            initialized_ = true;
            if (!ImGui_ImplVulkan_CreateFontsTexture()) throw std::runtime_error("ImGui font upload failed");
        } catch (...) {
            // Init registers the backend before allocating Vulkan children. The
            // factory's local owner also destroys this partially initialized state.
            initialized_ = ImGui::GetIO().BackendRendererUserData != nullptr;
            throw;
        }
    }
    void newFrame() override {
        if (minimumImageCount_ != swapchain_.minimumImageCount()) {
            ImGui_ImplVulkan_SetMinImageCount(swapchain_.minimumImageCount());
            minimumImageCount_ = swapchain_.minimumImageCount();
        }
        ImGui_ImplVulkan_NewFrame();
    }
    void beginFrame(std::uint32_t slot, std::uint32_t slotCount) override {
        if (!slotCount || slot >= slotCount) throw std::invalid_argument("GUI frame slot is out of range");
        currentSlot_ = slot;
        for (auto& [set, owner] : textures_) {
            (void)set;
            owner = slot;
        }
        auto retired = retiredTextures_.begin();
        while (retired != retiredTextures_.end()) {
            if (retired->ownerSlot != slot) {
                ++retired;
                continue;
            }
            ImGui_ImplVulkan_RemoveTexture(retired->set);
            retired = retiredTextures_.erase(retired);
        }
    }
    ImTextureID registerTexture(Texture& texture, Sampler& sampler) override {
        const auto set = ImGui_ImplVulkan_AddTexture(detail::nativeSampler(device_, sampler),
                                                     detail::nativeImageView(device_, texture),
                                                     VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        try {
            textures_.emplace(set, currentSlot_);
        } catch (...) {
            ImGui_ImplVulkan_RemoveTexture(set);
            throw;
        }
        return static_cast<ImTextureID>(reinterpret_cast<std::uintptr_t>(set));
    }
    void updateTexture(ImTextureID id, Texture& texture, Sampler& sampler) override {
        (void)updateTextureForFrame(id, texture, sampler);
    }
    ImTextureID updateTextureForFrame(ImTextureID id, Texture& texture, Sampler& sampler) override {
        const auto oldSet = descriptor(id);
        const auto newSet = ImGui_ImplVulkan_AddTexture(detail::nativeSampler(device_, sampler),
                                                        detail::nativeImageView(device_, texture),
                                                        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        const auto old = textures_.find(oldSet);
        if (old == textures_.end()) {
            ImGui_ImplVulkan_RemoveTexture(newSet);
            throw std::invalid_argument("Unknown GUI texture registration");
        }
        const auto [inserted, success] = textures_.emplace(newSet, currentSlot_);
        if (!success) {
            ImGui_ImplVulkan_RemoveTexture(newSet);
            throw std::runtime_error("Duplicate GUI texture descriptor");
        }
        try {
            // The active slot is the slot whose draw data may still reference
            // this descriptor, even when it was allocated by an earlier frame.
            retiredTextures_.push_back({oldSet, old->second});
        } catch (...) {
            textures_.erase(inserted);
            ImGui_ImplVulkan_RemoveTexture(newSet);
            throw;
        }
        textures_.erase(old);
        return static_cast<ImTextureID>(reinterpret_cast<std::uintptr_t>(newSet));
    }
    void unregisterTexture(ImTextureID id) override {
        const auto set = descriptor(id);
        const auto current = textures_.find(set);
        try {
            retiredTextures_.push_back({set, current->second});
        } catch (...) {
            throw;
        }
        textures_.erase(current);
    }
    void renderDrawData(CommandList& commands, ImDrawData* data) override {
        ImGui_ImplVulkan_RenderDrawData(data, detail::nativeCommandBuffer(device_, commands));
    }

private:
    VkDescriptorSet descriptor(ImTextureID id) const {
        const auto set = reinterpret_cast<VkDescriptorSet>(static_cast<std::uintptr_t>(id));
        if (!textures_.contains(set)) throw std::invalid_argument("Unknown GUI texture registration");
        return set;
    }
    Device& device_;
    Swapchain& swapchain_;
    detail::NativeContext native_;
    VkDescriptorPool pool_{};
    std::uint32_t minimumImageCount_{};
    std::uint32_t currentSlot_{};
    bool initialized_{};
    std::unordered_map<VkDescriptorSet, std::uint32_t> textures_;
    struct RetiredTexture {
        VkDescriptorSet set{};
        std::uint32_t ownerSlot{};
    };
    std::vector<RetiredTexture> retiredTextures_;
};
}

std::unique_ptr<gui::GuiRenderer> createVulkanGuiRenderer(Device& device, Swapchain& swapchain) {
    auto renderer = std::make_unique<VulkanGuiRenderer>(device, swapchain);
    renderer->initialize();
    return renderer;
}
}
