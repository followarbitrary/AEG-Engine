add_library(aeg_render STATIC
    public/VulkanRenderer.hpp
    public/RenderSettings.hpp
    public/AntialiasingSettings.hpp
    private/VulkanRenderer.cpp
    private/vulkan/VulkanContext.cpp
    private/vulkan/VulkanSwapchain.cpp
    private/vulkan/VulkanResources.cpp
    private/vulkan/VulkanDescriptors.cpp
    private/vulkan/VulkanPipeline.cpp
    private/scene/GpuModel.cpp
    private/scene/ScenePass.cpp
    private/viewport/ViewportTargets.cpp
    private/viewport/ViewportCapture.cpp
    private/antialiasing/Antialiasing.cpp
    private/antialiasing/TaaPass.cpp
    private/antialiasing/FxaaPass.cpp)
add_library(aeg::render ALIAS aeg_render)
target_include_directories(aeg_render
    PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/public"
    PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/private")
target_link_libraries(aeg_render
    PUBLIC aeg::assets aeg_imgui
    PRIVATE Vulkan::Vulkan glfw)
aeg_project_options(aeg_render)

set(aeg_shader_root "${CMAKE_CURRENT_SOURCE_DIR}/private/shaders")
set(aeg_shader_outputs)
foreach(shader model.vert model.frag fullscreen.vert fxaa.frag taa.frag)
    if(shader MATCHES "^model\\.")
        set(source "${aeg_shader_root}/scene/${shader}")
    else()
        set(source "${aeg_shader_root}/postprocess/${shader}")
    endif()
    set(output "${CMAKE_BINARY_DIR}/compiled-shaders/${shader}.spv")
    add_custom_command(OUTPUT "${output}"
        COMMAND "${CMAKE_COMMAND}" -E make_directory "${CMAKE_BINARY_DIR}/compiled-shaders"
        COMMAND Vulkan::glslc --target-env=vulkan1.2 "${source}" -o "${output}"
        DEPENDS "${source}" "${aeg_shader_root}/scene/normal_mapping.glsl"
        COMMENT "Compiling ${shader} to SPIR-V" VERBATIM)
    list(APPEND aeg_shader_outputs "${output}")
endforeach()
foreach(stage vert frag)
    set(source "${aeg_shader_root}/scene/model.${stage}")
    set(output "${CMAKE_BINARY_DIR}/compiled-shaders/model_taa.${stage}.spv")
    add_custom_command(OUTPUT "${output}"
        COMMAND "${CMAKE_COMMAND}" -E make_directory "${CMAKE_BINARY_DIR}/compiled-shaders"
        COMMAND Vulkan::glslc --target-env=vulkan1.2 -DAEG_TAA=1 "${source}" -o "${output}"
        DEPENDS "${source}" "${aeg_shader_root}/scene/normal_mapping.glsl"
        COMMENT "Compiling temporal model.${stage} to SPIR-V" VERBATIM)
    list(APPEND aeg_shader_outputs "${output}")
endforeach()
add_custom_target(aeg_shaders DEPENDS ${aeg_shader_outputs})
set(aeg_shader_outputs "${aeg_shader_outputs}" PARENT_SCOPE)
