#include <VulkanGuiBackend.hpp>
#include "../VulkanInterop.hpp"
#include <imgui_impl_vulkan.h>
#include <cstdint>
#include <memory>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <utility>
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
            // Destruction is the one place where waiting for the whole device
            // is allowed. It makes every descriptor safe before the ImGui
            // backend frees its descriptor sets.
            try {
                checkGui(vkDeviceWaitIdle(native_.device));
            } catch (...) {
            }
            for (const auto& [record, texture] : textures_) {
                (void)record;
                ImGui_ImplVulkan_RemoveTexture(texture->descriptor);
            }
            for (const auto& retired : retiredTextures_) ImGui_ImplVulkan_RemoveTexture(retired.set);
            textures_.clear();
            retiredTextures_.clear();
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
            auto record = std::make_unique<TextureRecord>();
            record->descriptor = set;
            record->ownerSlot = currentSlot_;
            const auto logical = record.get();
            textures_.emplace(logical, std::move(record));
            return static_cast<ImTextureID>(reinterpret_cast<std::uintptr_t>(logical));
        } catch (...) {
            ImGui_ImplVulkan_RemoveTexture(set);
            throw;
        }
    }
    void updateTexture(ImTextureID id, Texture& texture, Sampler& sampler) override {
        (void)updateTextureForFrame(id, texture, sampler);
    }
    ImTextureID updateTextureForFrame(ImTextureID id, Texture& texture, Sampler& sampler) override {
        auto* record = activeTexture(id);
        const auto oldSet = record->descriptor;
        const auto oldOwner = record->ownerSlot;
        const auto newSet = ImGui_ImplVulkan_AddTexture(detail::nativeSampler(device_, sampler),
                                                        detail::nativeImageView(device_, texture),
                                                        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        try {
            // The old set remains referenced by command buffers recorded in
            // its owner slot. Keep the logical token and replace only the
            // descriptor that draw submission sees.
            retiredTextures_.push_back({oldSet, oldOwner, {}});
        } catch (...) {
            ImGui_ImplVulkan_RemoveTexture(newSet);
            throw;
        }
        record->descriptor = newSet;
        record->ownerSlot = currentSlot_;
        return id;
    }
    void unregisterTexture(ImTextureID id) override {
        auto current = textures_.find(reinterpret_cast<TextureRecord*>(static_cast<std::uintptr_t>(id)));
        if (current == textures_.end()) throw std::invalid_argument("Unknown GUI texture registration");
        // Keep the logical record alive with the retired descriptor until its
        // owner slot has completed. This makes unregister safe even when it
        // happens immediately after recording a frame.
        const auto descriptor = current->second->descriptor;
        const auto ownerSlot = current->second->ownerSlot;
        retiredTextures_.push_back({descriptor, ownerSlot, {}});
        retiredTextures_.back().record = std::move(current->second);
        textures_.erase(current);
    }
    void renderDrawData(CommandList& commands, ImDrawData* data) override {
        std::vector<std::pair<ImDrawCmd*, ImTextureID>> patched;
        std::size_t commandCount = 0;
        for (int listIndex = 0; listIndex < data->CmdListsCount; ++listIndex)
            commandCount += static_cast<std::size_t>(data->CmdLists[listIndex]->CmdBuffer.Size);
        patched.reserve(commandCount);
        for (int listIndex = 0; listIndex < data->CmdListsCount; ++listIndex) {
            auto* list = data->CmdLists[listIndex];
            for (auto& command : list->CmdBuffer) {
                auto* logical = reinterpret_cast<TextureRecord*>(static_cast<std::uintptr_t>(command.TextureId));
                const auto texture = textures_.find(logical);
                if (texture == textures_.end()) continue;
                patched.emplace_back(&command, command.TextureId);
                command.TextureId = static_cast<ImTextureID>(reinterpret_cast<std::uintptr_t>(texture->second->descriptor));
            }
        }
        try {
            ImGui_ImplVulkan_RenderDrawData(data, detail::nativeCommandBuffer(device_, commands));
        } catch (...) {
            for (const auto& [command, id] : patched) command->TextureId = id;
            throw;
        }
        for (const auto& [command, id] : patched) command->TextureId = id;
    }

private:
    struct TextureRecord {
        VkDescriptorSet descriptor{};
        std::uint32_t ownerSlot{};
    };
    TextureRecord* activeTexture(ImTextureID id) const {
        auto* logical = reinterpret_cast<TextureRecord*>(static_cast<std::uintptr_t>(id));
        const auto texture = textures_.find(logical);
        if (texture == textures_.end()) throw std::invalid_argument("Unknown GUI texture registration");
        return texture->second.get();
    }
    Device& device_;
    Swapchain& swapchain_;
    detail::NativeContext native_;
    VkDescriptorPool pool_{};
    std::uint32_t minimumImageCount_{};
    std::uint32_t currentSlot_{};
    bool initialized_{};
    std::unordered_map<TextureRecord*, std::unique_ptr<TextureRecord>> textures_;
    struct RetiredTexture {
        VkDescriptorSet set{};
        std::uint32_t ownerSlot{};
        std::unique_ptr<TextureRecord> record;
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
