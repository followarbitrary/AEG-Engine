add_library(aeg_rhi_vulkan STATIC
    private/VmaImplementation.cpp
    private/device/VulkanDevice.cpp
    private/resources/VulkanResources.cpp
    private/bindings/VulkanBindings.cpp
    private/pipelines/VulkanPipelines.cpp
    private/commands/VulkanCommands.cpp
    private/presentation/VulkanSwapchain.cpp)
add_library(aeg::rhi_vulkan ALIAS aeg_rhi_vulkan)
target_include_directories(aeg_rhi_vulkan
    PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/public"
    PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/private")
target_link_libraries(aeg_rhi_vulkan PUBLIC aeg::rhi PRIVATE Vulkan::Vulkan aeg::vma)
aeg_project_options(aeg_rhi_vulkan)

add_library(aeg_rhi_vulkan_gui STATIC private/imgui/VulkanGuiRenderer.cpp)
add_library(aeg::rhi_vulkan_gui ALIAS aeg_rhi_vulkan_gui)
target_include_directories(aeg_rhi_vulkan_gui PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/public")
target_link_libraries(aeg_rhi_vulkan_gui PUBLIC aeg::gui aeg::rhi_vulkan
    PRIVATE aeg_imgui_vulkan Vulkan::Vulkan)
aeg_project_options(aeg_rhi_vulkan_gui)
