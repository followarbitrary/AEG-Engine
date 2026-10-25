cmake_minimum_required(VERSION 3.25)
project(AEGEngine VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_CXX_SCAN_FOR_MODULES OFF)
if(MSVC)
    # Some installations contain only localized compiler messages. Detect the
    # actual UTF-8 prefix instead of trusting a cache decoded with the ANSI page.
    file(WRITE "${CMAKE_BINARY_DIR}/showincludes-probe.cpp" "#include <stddef.h>\n")
    execute_process(COMMAND "${CMAKE_CXX_COMPILER}" /nologo /showIncludes /c
        "${CMAKE_BINARY_DIR}/showincludes-probe.cpp" "/Fo${CMAKE_BINARY_DIR}/showincludes-probe.obj"
        OUTPUT_VARIABLE aeg_include_probe ERROR_QUIET ENCODING UTF-8)
    string(REPLACE "\n" ";" aeg_probe_lines "${aeg_include_probe}")
    foreach(line IN LISTS aeg_probe_lines)
        if(line MATCHES "^([^:]*:[^:]*:)[ ]+[A-Za-z]:")
            set(CMAKE_CL_SHOWINCLUDES_PREFIX "${CMAKE_MATCH_1}")
            break()
        endif()
    endforeach()
endif()
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

find_package(Vulkan 1.2 REQUIRED COMPONENTS glslc)
include(cmake/Dependencies.cmake)
include(CTest)
option(AEG_ENABLE_GPU_TESTS "Run GPU smoke tests (requires a display and Vulkan device)" OFF)

function(aeg_project_options target)
    target_compile_definitions(${target} PUBLIC GLM_FORCE_RADIANS GLM_FORCE_DEPTH_ZERO_TO_ONE GLFW_INCLUDE_NONE)
    if(MSVC)
        target_compile_options(${target} PRIVATE /W4 /utf-8 /permissive-)
    else()
        target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic)
    endif()
    get_target_property(target_type ${target} TYPE)
    if(MINGW AND target_type STREQUAL "EXECUTABLE" AND AEG_MINGW_PTHREAD_DLL)
        add_custom_command(TARGET ${target} POST_BUILD
            COMMAND "${CMAKE_COMMAND}" -E copy_if_different
                "${AEG_MINGW_PTHREAD_DLL}" "$<TARGET_FILE_DIR:${target}>"
            COMMENT "Copying MinGW pthread runtime for ${target}" VERBATIM)
    endif()
endfunction()

add_library(aeg_core STATIC src/assets/Model.cpp src/core/OrbitCamera.cpp)
target_include_directories(aeg_core PUBLIC include)
target_include_directories(aeg_core SYSTEM PRIVATE "${stb_SOURCE_DIR}")
target_link_libraries(aeg_core PUBLIC glm::glm PRIVATE assimp::assimp)
aeg_project_options(aeg_core)

add_executable(aeg_editor src/main.cpp src/editor/Editor.cpp src/render/VulkanRenderer.cpp)
target_link_libraries(aeg_editor PRIVATE aeg_core aeg_imgui Vulkan::Vulkan glfw)
if(WIN32)
    target_link_libraries(aeg_editor PRIVATE shell32)
endif()
aeg_project_options(aeg_editor)

set(aeg_shader_outputs)
foreach(shader model.vert model.frag)
    set(output "${CMAKE_BINARY_DIR}/compiled-shaders/${shader}.spv")
    add_custom_command(OUTPUT "${output}"
        COMMAND "${CMAKE_COMMAND}" -E make_directory "${CMAKE_BINARY_DIR}/compiled-shaders"
        COMMAND Vulkan::glslc --target-env=vulkan1.2 "${PROJECT_SOURCE_DIR}/shaders/${shader}" -o "${output}"
        DEPENDS "${PROJECT_SOURCE_DIR}/shaders/${shader}"
        COMMENT "Compiling ${shader} to SPIR-V" VERBATIM)
    list(APPEND aeg_shader_outputs "${output}")
endforeach()
add_custom_target(aeg_shaders DEPENDS ${aeg_shader_outputs})
add_dependencies(aeg_editor aeg_shaders)
# An always-run copy target also refreshes assets when no C++ file changes.
add_custom_target(aeg_resources ALL
    COMMAND "${CMAKE_COMMAND}" -E make_directory "$<TARGET_FILE_DIR:aeg_editor>/shaders"
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different ${aeg_shader_outputs} "$<TARGET_FILE_DIR:aeg_editor>/shaders"
    COMMAND "${CMAKE_COMMAND}" -E copy_directory "${PROJECT_SOURCE_DIR}/assets" "$<TARGET_FILE_DIR:aeg_editor>/assets"
    DEPENDS aeg_shaders VERBATIM)
add_dependencies(aeg_editor aeg_resources)

if(BUILD_TESTING)
    add_executable(aeg_core_tests tests/CoreTests.cpp)
    target_link_libraries(aeg_core_tests PRIVATE aeg_core)
    target_compile_definitions(aeg_core_tests PRIVATE
        AEG_SOURCE_DIR="${PROJECT_SOURCE_DIR}"
        AEG_TEST_FIXTURES="${PROJECT_SOURCE_DIR}/tests/fixtures")
    aeg_project_options(aeg_core_tests)
    add_test(NAME aeg_core_tests COMMAND aeg_core_tests)
    set_tests_properties(aeg_core_tests PROPERTIES WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}" LABELS cpu)
    if(AEG_ENABLE_GPU_TESTS)
        add_executable(aeg_render_check tests/RenderSmoke.cpp)
        aeg_project_options(aeg_render_check)
        add_executable(aeg_backend_failure_test tests/BackendFailure.cpp src/render/VulkanRenderer.cpp)
        target_link_libraries(aeg_backend_failure_test PRIVATE aeg_core aeg_imgui Vulkan::Vulkan glfw)
        aeg_project_options(aeg_backend_failure_test)
        add_dependencies(aeg_backend_failure_test aeg_resources)
        add_test(NAME aeg_backend_failure COMMAND aeg_backend_failure_test "$<TARGET_FILE_DIR:aeg_editor>/shaders")
        set_tests_properties(aeg_backend_failure PROPERTIES LABELS gpu TIMEOUT 60)
        add_executable(aeg_renderer_lifecycle_test tests/RendererLifecycle.cpp src/render/VulkanRenderer.cpp)
        target_link_libraries(aeg_renderer_lifecycle_test PRIVATE aeg_core aeg_imgui Vulkan::Vulkan glfw)
        aeg_project_options(aeg_renderer_lifecycle_test)
        add_dependencies(aeg_renderer_lifecycle_test aeg_resources)
        add_test(NAME aeg_renderer_lifecycle COMMAND aeg_renderer_lifecycle_test
            "$<TARGET_FILE_DIR:aeg_editor>" "${PROJECT_SOURCE_DIR}/tests/fixtures/embedded.gltf" "${CMAKE_BINARY_DIR}/lifecycle.ppm")
        set_tests_properties(aeg_renderer_lifecycle PROPERTIES LABELS gpu TIMEOUT 60)
        add_test(NAME aeg_gpu_smoke COMMAND "${CMAKE_COMMAND}"
            "-DEDITOR=$<TARGET_FILE:aeg_editor>"
            "-DCHECKER=$<TARGET_FILE:aeg_render_check>"
            "-DCAPTURE=${CMAKE_BINARY_DIR}/smoke.ppm"
            -P "${PROJECT_SOURCE_DIR}/tests/GpuSmoke.cmake")
        set_tests_properties(aeg_gpu_smoke PROPERTIES LABELS gpu TIMEOUT 90)
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
                -P "${PROJECT_SOURCE_DIR}/tests/GpuSmoke.cmake")
            set_tests_properties("aeg_gpu_${fixture}" PROPERTIES LABELS gpu TIMEOUT 90)
        endforeach()
        foreach(fixture pmx-utf8 pmx-utf16 pmx-alpha pmx-extra-uv)
            add_test(NAME "aeg_gpu_${fixture}" COMMAND "${CMAKE_COMMAND}"
                "-DEDITOR=$<TARGET_FILE:aeg_editor>"
                "-DCHECKER=$<TARGET_FILE:aeg_render_check>"
                "-DCAPTURE=${CMAKE_BINARY_DIR}/${fixture}.ppm"
                "-DMODEL=${PROJECT_SOURCE_DIR}/tests/fixtures/${fixture}.pmx"
                -P "${PROJECT_SOURCE_DIR}/tests/GpuSmoke.cmake")
            set_tests_properties("aeg_gpu_${fixture}" PROPERTIES LABELS gpu TIMEOUT 90)
        endforeach()
    endif()
endif()
