include(FetchContent)
file(MAKE_DIRECTORY "${PROJECT_SOURCE_DIR}/build")
file(LOCK "${PROJECT_SOURCE_DIR}/build/fetch-content.lock" GUARD FILE TIMEOUT 120)

# Share immutable sources across Debug and Release without sharing build products.
set(FETCHCONTENT_BASE_DIR "${PROJECT_SOURCE_DIR}/build/_deps" CACHE PATH "Dependency sources" FORCE)
set(BUILD_SHARED_LIBS OFF CACHE BOOL "Static third-party libraries" FORCE)
set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
set(GLFW_INSTALL OFF CACHE BOOL "" FORCE)
set(ASSIMP_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(ASSIMP_BUILD_ASSIMP_TOOLS OFF CACHE BOOL "" FORCE)
set(ASSIMP_BUILD_SAMPLES OFF CACHE BOOL "" FORCE)
set(ASSIMP_INSTALL OFF CACHE BOOL "" FORCE)
set(ASSIMP_WARNINGS_AS_ERRORS OFF CACHE BOOL "" FORCE)
set(ASSIMP_BUILD_ALL_IMPORTERS_BY_DEFAULT OFF CACHE BOOL "" FORCE)
set(ASSIMP_BUILD_OBJ_IMPORTER ON CACHE BOOL "" FORCE)
set(ASSIMP_BUILD_GLTF_IMPORTER ON CACHE BOOL "" FORCE)
set(ASSIMP_BUILD_MMD_IMPORTER ON CACHE BOOL "" FORCE)
set(ASSIMP_NO_EXPORT ON CACHE BOOL "" FORCE)
set(ASSIMP_BUILD_ZLIB ON CACHE BOOL "" FORCE)

FetchContent_Declare(glfw
    SUBBUILD_DIR "${CMAKE_BINARY_DIR}/_deps/glfw-subbuild"
    DOWNLOAD_DIR "${PROJECT_SOURCE_DIR}/build/downloads"
    DOWNLOAD_NAME glfw.tar.gz
    BINARY_DIR "${CMAKE_BINARY_DIR}/_deps/glfw-build"
    URL https://codeload.github.com/glfw/glfw/tar.gz/refs/tags/3.4
    URL_HASH SHA256=c038d34200234d071fae9345bc455e4a8f2f544ab60150765d7704e08f3dac01
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
FetchContent_Declare(assimp
    SUBBUILD_DIR "${CMAKE_BINARY_DIR}/_deps/assimp-subbuild"
    DOWNLOAD_DIR "${PROJECT_SOURCE_DIR}/build/downloads"
    DOWNLOAD_NAME assimp.tar.gz
    BINARY_DIR "${CMAKE_BINARY_DIR}/_deps/assimp-build"
    URL https://codeload.github.com/assimp/assimp/tar.gz/refs/tags/v5.4.3
    URL_HASH SHA256=66dfbaee288f2bc43172440a55d0235dfc7bf885dda6435c038e8000e79582cb
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
FetchContent_Declare(imgui
    SUBBUILD_DIR "${CMAKE_BINARY_DIR}/_deps/imgui-subbuild"
    DOWNLOAD_DIR "${PROJECT_SOURCE_DIR}/build/downloads"
    DOWNLOAD_NAME imgui.tar.gz
    BINARY_DIR "${CMAKE_BINARY_DIR}/_deps/imgui-build"
    URL https://codeload.github.com/ocornut/imgui/tar.gz/refs/tags/v1.91.9b-docking
    URL_HASH SHA256=466fdef9b18de15f0bb6e288e3d00ffa3d82200ec458ce5e4f724a161d9528a5
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
FetchContent_Declare(stb
    SUBBUILD_DIR "${CMAKE_BINARY_DIR}/_deps/stb-subbuild"
    DOWNLOAD_DIR "${PROJECT_SOURCE_DIR}/build/downloads"
    DOWNLOAD_NAME stb.tar.gz
    BINARY_DIR "${CMAKE_BINARY_DIR}/_deps/stb-build"
    URL https://codeload.github.com/nothings/stb/tar.gz/003501f6d7a8668ce8b6d875e34dd97ad0d5d5b5
    URL_HASH SHA256=a6c31950b3bdf05eb92258768ba2a11bd44449490574b1aac67028452288c40d
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE)

FetchContent_Declare(imguizmo
    SUBBUILD_DIR "${CMAKE_BINARY_DIR}/_deps/imguizmo-subbuild"
    DOWNLOAD_DIR "${PROJECT_SOURCE_DIR}/build/downloads"
    DOWNLOAD_NAME imguizmo.tar.gz
    BINARY_DIR "${CMAKE_BINARY_DIR}/_deps/imguizmo-build"
    URL https://codeload.github.com/CedricGuillemet/ImGuizmo/tar.gz/b796ac3b861afc6e91ca74e4611effd9c9527367
    URL_HASH SHA256=330072b157ee1fe1f4e2fce0d07cec89c734b8d25cfa09793708f71c02d6784e
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
    # Only consume the core sources; upstream examples fetch their own ImGui.
    SOURCE_SUBDIR aeg-source-only)

FetchContent_MakeAvailable(glfw assimp imgui stb imguizmo)
include("${CMAKE_CURRENT_LIST_DIR}/ApplyAssimpMmdPatch.cmake")
include("${CMAKE_CURRENT_LIST_DIR}/ApplyImGuiVulkanPatch.cmake")
include("${CMAKE_CURRENT_LIST_DIR}/ApplyImGuizmoPatch.cmake")

if(MINGW)
    # Assimp 5.4.3 exports a blanket -Bstatic plus manual stdc++/winpthread
    # libraries. CLion's MinGW 15.2 static winpthread needs UCRT symbols while
    # its compiler defaults to MSVCRT. Let GCC choose the pthread import library
    # and its normal link ordering; keep Assimp's static GCC/C++ runtime options.
    foreach(property LINK_LIBRARIES INTERFACE_LINK_LIBRARIES)
        get_target_property(assimp_link_items assimp ${property})
        if(assimp_link_items)
            list(REMOVE_ITEM assimp_link_items "-Wl,-Bstatic" "-lstdc++" "-lwinpthread")
            set_property(TARGET assimp PROPERTY ${property} "${assimp_link_items}")
        endif()
    endforeach()

    # The resulting executable needs this runtime DLL even outside CLion's
    # compiler environment. Discover it beside the selected compiler, not PATH.
    get_filename_component(aeg_mingw_bin "${CMAKE_CXX_COMPILER}" DIRECTORY)
    find_file(AEG_MINGW_PTHREAD_DLL NAMES libwinpthread-1.dll
        PATHS "${aeg_mingw_bin}" NO_DEFAULT_PATH NO_CACHE)
endif()

add_library(aeg_imgui_core STATIC
    "${imgui_SOURCE_DIR}/imgui.cpp"
    "${imgui_SOURCE_DIR}/imgui_draw.cpp"
    "${imgui_SOURCE_DIR}/imgui_tables.cpp"
    "${imgui_SOURCE_DIR}/imgui_widgets.cpp")
target_include_directories(aeg_imgui_core SYSTEM PUBLIC "${imgui_SOURCE_DIR}")
add_library(aeg_imguizmo STATIC "${imguizmo_SOURCE_DIR}/src/ImGuizmo.cpp")
target_include_directories(aeg_imguizmo SYSTEM PUBLIC "${imguizmo_SOURCE_DIR}/src")
target_link_libraries(aeg_imguizmo PUBLIC aeg_imgui_core)

add_library(aeg_imgui_glfw STATIC "${imgui_SOURCE_DIR}/backends/imgui_impl_glfw.cpp")
target_include_directories(aeg_imgui_glfw SYSTEM PUBLIC "${imgui_SOURCE_DIR}/backends")
target_link_libraries(aeg_imgui_glfw PUBLIC aeg_imgui_core PRIVATE glfw)
target_compile_definitions(aeg_imgui_glfw PUBLIC GLFW_INCLUDE_NONE)

add_library(aeg_imgui_vulkan STATIC "${imgui_SOURCE_DIR}/backends/imgui_impl_vulkan.cpp")
target_include_directories(aeg_imgui_vulkan SYSTEM PUBLIC "${imgui_SOURCE_DIR}/backends")
target_link_libraries(aeg_imgui_vulkan PUBLIC aeg_imgui_core PRIVATE Vulkan::Vulkan)
if(WIN32)
    # ImGui 1.91 disables native IME integration for non-MSVC unless opted in.
    target_compile_definitions(aeg_imgui_core PRIVATE IMGUI_ENABLE_WIN32_DEFAULT_IME_FUNCTIONS)
    target_link_libraries(aeg_imgui_core PUBLIC imm32)
endif()
