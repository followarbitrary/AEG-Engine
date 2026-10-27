function(aeg_compile_vulkan_shaders target_name source_root output_variable)
    set(embed_script "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/EmbedSpirv.cmake")
    set(shader_outputs)
    set(hdr_include "${source_root}/common/hdr.glsl")
    set(scene_includes
        "${hdr_include}"
        "${source_root}/scene/normal_mapping.glsl"
        "${source_root}/scene/ibl_lighting.glsl"
        "${source_root}/scene/pbr_lighting.glsl"
        "${source_root}/scene/punctual_lighting.glsl"
        "${source_root}/scene/scene_uniform.glsl"
        "${source_root}/scene/shadow_sampling.glsl")
    foreach(shader model.vert model.frag shadow.vert shadow.frag fullscreen.vert fxaa.frag taa.frag tonemap.frag
                   environment.comp irradiance.comp prefilter.comp brdf.comp)
        if(shader MATCHES "\\.comp$")
            set(source "${source_root}/ibl/${shader}")
            set(includes "${source_root}/ibl/shared.glsl")
        elseif(shader MATCHES "^(model|shadow)\\.")
            set(source "${source_root}/scene/${shader}")
            set(includes ${scene_includes})
        else()
            set(source "${source_root}/postprocess/${shader}")
            set(includes "${hdr_include}")
        endif()
        set(output "${CMAKE_BINARY_DIR}/compiled-shaders/${shader}.spv")
        add_custom_command(OUTPUT "${output}"
            COMMAND "${CMAKE_COMMAND}" -E make_directory "${CMAKE_BINARY_DIR}/compiled-shaders"
            COMMAND Vulkan::glslc --target-env=vulkan1.2 "${source}" -o "${output}"
            DEPENDS "${source}" ${includes}
            COMMENT "Compiling ${shader} to SPIR-V" VERBATIM)
        set(header "${CMAKE_BINARY_DIR}/generated/embedded_shaders/${shader}.spv.hpp")
        string(REPLACE "." "_" symbol "${shader}")
        add_custom_command(OUTPUT "${header}"
            COMMAND "${CMAKE_COMMAND}" "-DINPUT=${output}" "-DOUTPUT=${header}" "-DSYMBOL=${symbol}"
                -P "${embed_script}"
            DEPENDS "${output}" "${embed_script}"
            COMMENT "Embedding ${shader} SPIR-V" VERBATIM)
        list(APPEND shader_outputs "${header}")
    endforeach()
    foreach(stage vert frag)
        set(source "${source_root}/scene/model.${stage}")
        set(output "${CMAKE_BINARY_DIR}/compiled-shaders/model_taa.${stage}.spv")
        add_custom_command(OUTPUT "${output}"
            COMMAND "${CMAKE_COMMAND}" -E make_directory "${CMAKE_BINARY_DIR}/compiled-shaders"
            COMMAND Vulkan::glslc --target-env=vulkan1.2 -DAEG_TAA=1 "${source}" -o "${output}"
            DEPENDS "${source}" ${scene_includes}
            COMMENT "Compiling temporal model.${stage} to SPIR-V" VERBATIM)
        set(header "${CMAKE_BINARY_DIR}/generated/embedded_shaders/model_taa.${stage}.spv.hpp")
        add_custom_command(OUTPUT "${header}"
            COMMAND "${CMAKE_COMMAND}" "-DINPUT=${output}" "-DOUTPUT=${header}" "-DSYMBOL=model_taa_${stage}"
                -P "${embed_script}"
            DEPENDS "${output}" "${embed_script}"
            COMMENT "Embedding temporal model.${stage} SPIR-V" VERBATIM)
        list(APPEND shader_outputs "${header}")
    endforeach()
    add_custom_target(${target_name} DEPENDS ${shader_outputs})
    set(${output_variable} "${shader_outputs}" PARENT_SCOPE)
endfunction()
