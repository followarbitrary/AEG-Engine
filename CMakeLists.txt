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
    if(MINGW AND target_type STREQUAL "EXECUTABLE")
        target_link_options(${target} PRIVATE -static-libgcc -static-libstdc++)
    endif()
    if(MINGW AND target_type STREQUAL "EXECUTABLE" AND AEG_MINGW_PTHREAD_DLL)
        add_custom_command(TARGET ${target} POST_BUILD
            COMMAND "${CMAKE_COMMAND}" -E copy_if_different
                "${AEG_MINGW_PTHREAD_DLL}" "$<TARGET_FILE_DIR:${target}>"
            COMMENT "Copying MinGW pthread runtime for ${target}" VERBATIM)
    endif()
endfunction()

add_subdirectory(engine/foundation)
add_subdirectory(engine/camera)
add_subdirectory(engine/assets)
add_subdirectory(engine/render)
add_subdirectory(editor)
add_subdirectory(apps/editor)

if(BUILD_TESTING)
    add_subdirectory(tests)
endif()