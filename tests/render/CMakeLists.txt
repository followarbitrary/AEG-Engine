add_executable(aeg_pbr_lighting_test PbrLighting.cpp)
target_link_libraries(aeg_pbr_lighting_test PRIVATE aeg::editor aeg::rhi_vulkan aeg::rhi_vulkan_gui glfw)
aeg_project_options(aeg_pbr_lighting_test)
add_dependencies(aeg_pbr_lighting_test aeg_resources)
add_test(NAME aeg_pbr_lighting COMMAND aeg_pbr_lighting_test "${CMAKE_BINARY_DIR}/pbr-lighting")
set_tests_properties(aeg_pbr_lighting PROPERTIES LABELS gpu TIMEOUT 120)

add_executable(aeg_transparency_test Transparency.cpp)
target_link_libraries(aeg_transparency_test PRIVATE aeg::editor aeg::rhi_vulkan aeg::rhi_vulkan_gui glfw)
target_compile_definitions(aeg_transparency_test PRIVATE AEG_TEST_FIXTURES="${PROJECT_SOURCE_DIR}/tests/fixtures")
aeg_project_options(aeg_transparency_test)
add_dependencies(aeg_transparency_test aeg_resources)
add_test(NAME aeg_transparency COMMAND aeg_transparency_test "${CMAKE_BINARY_DIR}/transparency")
set_tests_properties(aeg_transparency PROPERTIES LABELS gpu TIMEOUT 120)

add_executable(aeg_clip_conventions_test ClipConventions.cpp)
target_link_libraries(aeg_clip_conventions_test PRIVATE aeg::editor aeg::rhi_vulkan aeg::rhi_vulkan_gui glfw)
aeg_project_options(aeg_clip_conventions_test)
add_dependencies(aeg_clip_conventions_test aeg_resources)
add_test(NAME aeg_clip_conventions COMMAND aeg_clip_conventions_test
    "${CMAKE_BINARY_DIR}/clip-conventions")
set_tests_properties(aeg_clip_conventions PROPERTIES LABELS gpu TIMEOUT 120)

add_executable(aeg_depth_precision_test DepthPrecision.cpp)
target_link_libraries(aeg_depth_precision_test PRIVATE aeg::editor aeg::rhi_vulkan aeg::rhi_vulkan_gui aeg::camera glfw)
aeg_project_options(aeg_depth_precision_test)
add_dependencies(aeg_depth_precision_test aeg_resources)
add_test(NAME aeg_depth_precision COMMAND aeg_depth_precision_test
    "${CMAKE_BINARY_DIR}/depth-precision")
set_tests_properties(aeg_depth_precision PROPERTIES LABELS gpu TIMEOUT 90)

add_executable(aeg_normal_mapping_test NormalMapping.cpp)
target_link_libraries(aeg_normal_mapping_test PRIVATE aeg::editor aeg::rhi_vulkan aeg::rhi_vulkan_gui glfw)
target_compile_definitions(aeg_normal_mapping_test PRIVATE AEG_TEST_FIXTURES="${PROJECT_SOURCE_DIR}/tests/fixtures")
aeg_project_options(aeg_normal_mapping_test)
add_dependencies(aeg_normal_mapping_test aeg_resources)
add_test(NAME aeg_normal_mapping COMMAND aeg_normal_mapping_test
    "${CMAKE_BINARY_DIR}/normal-mapping")
set_tests_properties(aeg_normal_mapping PROPERTIES LABELS gpu TIMEOUT 90)

add_executable(aeg_temporal_aa_test TemporalAA.cpp)
target_link_libraries(aeg_temporal_aa_test PRIVATE aeg::editor aeg::rhi_vulkan aeg::rhi_vulkan_gui glfw)
aeg_project_options(aeg_temporal_aa_test)
add_dependencies(aeg_temporal_aa_test aeg_resources)
add_test(NAME aeg_temporal_aa COMMAND aeg_temporal_aa_test
    "${CMAKE_BINARY_DIR}/temporal-aa")
set_tests_properties(aeg_temporal_aa PROPERTIES LABELS gpu TIMEOUT 90)

add_executable(aeg_antialiasing_test Antialiasing.cpp)
target_link_libraries(aeg_antialiasing_test PRIVATE aeg::editor aeg::rhi_vulkan aeg::rhi_vulkan_gui glfw)
aeg_project_options(aeg_antialiasing_test)
add_dependencies(aeg_antialiasing_test aeg_resources)
add_test(NAME aeg_antialiasing COMMAND aeg_antialiasing_test
    "${CMAKE_BINARY_DIR}/antialiasing")
set_tests_properties(aeg_antialiasing PROPERTIES LABELS gpu TIMEOUT 90)

add_executable(aeg_render_check RenderSmoke.cpp)
aeg_project_options(aeg_render_check)

# Keep loader-symbol failure injection in the executable. The renderer itself
# is compiled once into aeg_render and linked by every GPU test.
add_executable(aeg_backend_failure_test BackendFailure.cpp)
target_link_libraries(aeg_backend_failure_test PRIVATE aeg::editor aeg::rhi_vulkan aeg::rhi_vulkan_gui Vulkan::Vulkan glfw)
aeg_project_options(aeg_backend_failure_test)
add_dependencies(aeg_backend_failure_test aeg_resources)
add_test(NAME aeg_backend_failure COMMAND aeg_backend_failure_test)
set_tests_properties(aeg_backend_failure PROPERTIES LABELS gpu TIMEOUT 60)

add_executable(aeg_renderer_lifecycle_test RendererLifecycle.cpp)
target_link_libraries(aeg_renderer_lifecycle_test PRIVATE aeg::editor aeg::rhi_vulkan aeg::rhi_vulkan_gui aeg::camera glfw)
aeg_project_options(aeg_renderer_lifecycle_test)
add_dependencies(aeg_renderer_lifecycle_test aeg_resources)
add_test(NAME aeg_renderer_lifecycle COMMAND aeg_renderer_lifecycle_test
    "$<TARGET_FILE_DIR:aeg_editor>" "${PROJECT_SOURCE_DIR}/tests/fixtures/embedded.gltf" "${CMAKE_BINARY_DIR}/lifecycle.ppm")
set_tests_properties(aeg_renderer_lifecycle PROPERTIES LABELS gpu TIMEOUT 60)

add_test(NAME aeg_gpu_smoke COMMAND "${CMAKE_COMMAND}"
    "-DEDITOR=$<TARGET_FILE:aeg_editor>"
    "-DCHECKER=$<TARGET_FILE:aeg_render_check>"
    "-DCAPTURE=${CMAKE_BINARY_DIR}/smoke.ppm"
    -P "${PROJECT_SOURCE_DIR}/cmake/GpuSmoke.cmake")
set_tests_properties(aeg_gpu_smoke PROPERTIES LABELS gpu TIMEOUT 90)
add_test(NAME aeg_embedded_shader_deployment COMMAND "${CMAKE_COMMAND}"
    "-DEDITOR=$<TARGET_FILE:aeg_editor>"
    "-DCHECKER=$<TARGET_FILE:aeg_render_check>"
    "-DMODEL=${PROJECT_SOURCE_DIR}/tests/fixtures/embedded.gltf"
    "-DWORK_DIR=${CMAKE_BINARY_DIR}/embedded-shader-deployment"
    -P "${PROJECT_SOURCE_DIR}/cmake/EmbeddedShaderSmoke.cmake")
set_tests_properties(aeg_embedded_shader_deployment PROPERTIES LABELS gpu TIMEOUT 240)
foreach(fixture alpha-opaque alpha-default alpha-mask alpha-mask-custom uri-spaces uri-unicode)
    set(expect_blank OFF)
    if(fixture STREQUAL "alpha-mask")
        set(expect_blank ON)
    endif()
    add_test(NAME "aeg_gpu_${fixture}" COMMAND "${CMAKE_COMMAND}"
        "-DEDITOR=$<TARGET_FILE:aeg_editor>"
        "-DCHECKER=$<TARGET_FILE:aeg_render_check>"
        "-DCAPTURE=${CMAKE_BINARY_DIR}/${fixture}.ppm"
        "-DMODEL=${PROJECT_SOURCE_DIR}/tests/fixtures/${fixture}.gltf"
        "-DEXPECT_BLANK=${expect_blank}"
        -P "${PROJECT_SOURCE_DIR}/cmake/GpuSmoke.cmake")
    set_tests_properties("aeg_gpu_${fixture}" PROPERTIES LABELS gpu TIMEOUT 90)
endforeach()
# Partial opacity needs controlled lighting/background for a meaningful contrast check.
add_test(NAME aeg_gpu_alpha-blend COMMAND aeg_transparency_test
    "${CMAKE_BINARY_DIR}/gltf-alpha-smoke" --gltf-smoke)
set_tests_properties(aeg_gpu_alpha-blend PROPERTIES LABELS gpu TIMEOUT 90)
add_test(NAME aeg_gpu_pmx-alpha COMMAND aeg_transparency_test
    "${CMAKE_BINARY_DIR}/pmx-alpha-smoke" --pmx-smoke)
set_tests_properties(aeg_gpu_pmx-alpha PROPERTIES LABELS gpu TIMEOUT 90)
foreach(fixture pmx-utf8 pmx-utf16 pmx-extra-uv)
    add_test(NAME "aeg_gpu_${fixture}" COMMAND "${CMAKE_COMMAND}"
        "-DEDITOR=$<TARGET_FILE:aeg_editor>"
        "-DCHECKER=$<TARGET_FILE:aeg_render_check>"
        "-DCAPTURE=${CMAKE_BINARY_DIR}/${fixture}.ppm"
        "-DMODEL=${PROJECT_SOURCE_DIR}/tests/fixtures/${fixture}.pmx"
        -P "${PROJECT_SOURCE_DIR}/cmake/GpuSmoke.cmake")
    set_tests_properties("aeg_gpu_${fixture}" PROPERTIES LABELS gpu TIMEOUT 90)
endforeach()
add_executable(aeg_punctual_lights_test PunctualLights.cpp)
target_link_libraries(aeg_punctual_lights_test PRIVATE aeg::editor aeg::rhi_vulkan aeg::rhi_vulkan_gui glfw)
aeg_project_options(aeg_punctual_lights_test)
add_dependencies(aeg_punctual_lights_test aeg_resources)
add_test(NAME aeg_punctual_lights COMMAND aeg_punctual_lights_test "${CMAKE_BINARY_DIR}/punctual-lights")
set_tests_properties(aeg_punctual_lights PROPERTIES LABELS gpu TIMEOUT 120)

add_executable(aeg_shadow_mapping_test ShadowMapping.cpp)
target_link_libraries(aeg_shadow_mapping_test PRIVATE aeg::editor aeg::rhi_vulkan aeg::rhi_vulkan_gui glfw)
aeg_project_options(aeg_shadow_mapping_test)
add_dependencies(aeg_shadow_mapping_test aeg_resources)
add_test(NAME aeg_shadow_mapping COMMAND aeg_shadow_mapping_test "${CMAKE_BINARY_DIR}/shadow-mapping")
set_tests_properties(aeg_shadow_mapping PROPERTIES LABELS gpu TIMEOUT 120)
add_executable(aeg_cascaded_shadows_test CascadedShadows.cpp)
target_link_libraries(aeg_cascaded_shadows_test PRIVATE aeg::editor aeg::rhi_vulkan aeg::rhi_vulkan_gui glfw)
aeg_project_options(aeg_cascaded_shadows_test)
add_dependencies(aeg_cascaded_shadows_test aeg_resources)
add_test(NAME aeg_cascaded_shadows COMMAND aeg_cascaded_shadows_test "${CMAKE_BINARY_DIR}/cascaded-shadows")
set_tests_properties(aeg_cascaded_shadows PROPERTIES LABELS gpu TIMEOUT 150)
add_executable(aeg_ibl_precomputation_test IblPrecomputation.cpp)
target_link_libraries(aeg_ibl_precomputation_test PRIVATE aeg::render aeg::rhi_vulkan glfw)
target_include_directories(aeg_ibl_precomputation_test PRIVATE "${PROJECT_SOURCE_DIR}/engine/render/private")
aeg_project_options(aeg_ibl_precomputation_test)
add_test(NAME aeg_ibl_precomputation COMMAND aeg_ibl_precomputation_test)
set_tests_properties(aeg_ibl_precomputation PROPERTIES LABELS gpu TIMEOUT 180)

add_executable(aeg_image_based_lighting_test ImageBasedLighting.cpp)
target_link_libraries(aeg_image_based_lighting_test PRIVATE aeg::editor aeg::rhi_vulkan aeg::rhi_vulkan_gui glfw)
aeg_project_options(aeg_image_based_lighting_test)
add_dependencies(aeg_image_based_lighting_test aeg_resources)
add_test(NAME aeg_image_based_lighting COMMAND aeg_image_based_lighting_test "${CMAKE_BINARY_DIR}/image-based-lighting")
set_tests_properties(aeg_image_based_lighting PROPERTIES LABELS gpu TIMEOUT 180)

add_executable(aeg_compute_contracts_test ComputeContracts.cpp)
target_link_libraries(aeg_compute_contracts_test PRIVATE aeg::render aeg::rhi_vulkan glfw)
target_include_directories(aeg_compute_contracts_test PRIVATE "${PROJECT_SOURCE_DIR}/engine/render/private")
aeg_project_options(aeg_compute_contracts_test)
add_test(NAME aeg_compute_contracts COMMAND aeg_compute_contracts_test)
set_tests_properties(aeg_compute_contracts PROPERTIES LABELS gpu TIMEOUT 120)
add_executable(aeg_hdr_tone_mapping_test HdrToneMapping.cpp)
target_link_libraries(aeg_hdr_tone_mapping_test PRIVATE aeg::render aeg::rhi_vulkan glfw)
aeg_project_options(aeg_hdr_tone_mapping_test)
add_test(NAME aeg_hdr_tone_mapping COMMAND aeg_hdr_tone_mapping_test "${CMAKE_BINARY_DIR}/hdr-tone-mapping")
set_tests_properties(aeg_hdr_tone_mapping PROPERTIES LABELS gpu TIMEOUT 180)
