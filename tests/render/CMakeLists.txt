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
foreach(fixture alpha-opaque alpha-default alpha-mask alpha-mask-custom alpha-blend uri-spaces uri-unicode)
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
foreach(fixture pmx-utf8 pmx-utf16 pmx-alpha pmx-extra-uv)
    add_test(NAME "aeg_gpu_${fixture}" COMMAND "${CMAKE_COMMAND}"
        "-DEDITOR=$<TARGET_FILE:aeg_editor>"
        "-DCHECKER=$<TARGET_FILE:aeg_render_check>"
        "-DCAPTURE=${CMAKE_BINARY_DIR}/${fixture}.ppm"
        "-DMODEL=${PROJECT_SOURCE_DIR}/tests/fixtures/${fixture}.pmx"
        -P "${PROJECT_SOURCE_DIR}/cmake/GpuSmoke.cmake")
    set_tests_properties("aeg_gpu_${fixture}" PROPERTIES LABELS gpu TIMEOUT 90)
endforeach()
