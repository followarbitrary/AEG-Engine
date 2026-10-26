function(aeg_compile_vulkan_shaders target_name source_root output_variable)
    set(shader_outputs)
    foreach(shader model.vert model.frag fullscreen.vert fxaa.frag taa.frag)
        if(shader MATCHES "^model\\.")
            set(source "${source_root}/scene/${shader}")
        else()
            set(source "${source_root}/postprocess/${shader}")
        endif()
        set(output "${CMAKE_BINARY_DIR}/compiled-shaders/${shader}.spv")
        add_custom_command(OUTPUT "${output}"
            COMMAND "${CMAKE_COMMAND}" -E make_directory "${CMAKE_BINARY_DIR}/compiled-shaders"
            COMMAND Vulkan::glslc --target-env=vulkan1.2 "${source}" -o "${output}"
            DEPENDS "${source}" "${source_root}/scene/normal_mapping.glsl"
            COMMENT "Compiling ${shader} to SPIR-V" VERBATIM)
        list(APPEND shader_outputs "${output}")
    endforeach()
    foreach(stage vert frag)
        set(source "${source_root}/scene/model.${stage}")
        set(output "${CMAKE_BINARY_DIR}/compiled-shaders/model_taa.${stage}.spv")
        add_custom_command(OUTPUT "${output}"
            COMMAND "${CMAKE_COMMAND}" -E make_directory "${CMAKE_BINARY_DIR}/compiled-shaders"
            COMMAND Vulkan::glslc --target-env=vulkan1.2 -DAEG_TAA=1 "${source}" -o "${output}"
            DEPENDS "${source}" "${source_root}/scene/normal_mapping.glsl"
            COMMENT "Compiling temporal model.${stage} to SPIR-V" VERBATIM)
        list(APPEND shader_outputs "${output}")
    endforeach()
    add_custom_target(${target_name} DEPENDS ${shader_outputs})
    set(${output_variable} "${shader_outputs}" PARENT_SCOPE)
endfunction()
