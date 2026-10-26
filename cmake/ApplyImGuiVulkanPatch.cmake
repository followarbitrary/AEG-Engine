# Exception cleanup for the pinned ImGui v1.91.9b-docking Vulkan font uploader.
# Include after FetchContent_MakeAvailable while holding the dependency lock.
# Full-source hashes (normalized by file(READ)) accept only the pinned upstream
# file or this exact patch, including CMake's Windows newline conversion.
if(NOT DEFINED imgui_SOURCE_DIR)
    message(FATAL_ERROR "The ImGui Vulkan patch requires populated ImGui sources")
endif()
set(_aeg_imgui_vulkan_path "${imgui_SOURCE_DIR}/backends/imgui_impl_vulkan.cpp")
file(READ "${_aeg_imgui_vulkan_path}" _aeg_imgui_vulkan_source)
string(SHA256 _aeg_imgui_vulkan_hash "${_aeg_imgui_vulkan_source}")
if(_aeg_imgui_vulkan_hash STREQUAL "66fda3e62abfbb644759408283744a5783ffcef2156a9c82877ce0f91d5f9381")
    return()
endif()
if(NOT _aeg_imgui_vulkan_hash STREQUAL "616b1bb715fa3c987ae1b838674099434e8c59f73fec29a861fc8ed810d3d170")
    message(FATAL_ERROR "Re-review the AEG font staging patch: ImGui v1.91.9b-docking Vulkan source differs from the pinned original or patch")
endif()

string(REPLACE [=[    // Create the Upload Buffer:
    VkDeviceMemory upload_buffer_memory;
    VkBuffer upload_buffer;]=] [=[    // AEG: font staging also unwinds when CheckVkResultFn throws.
    VkDeviceMemory upload_buffer_memory = VK_NULL_HANDLE;
    VkBuffer upload_buffer = VK_NULL_HANDLE;
    struct FontUploadGuard
    {
        VkDevice device;
        const VkAllocationCallbacks* allocator;
        VkDeviceMemory& memory;
        VkBuffer& buffer;
        bool mapped = false;
        ~FontUploadGuard()
        {
            if (mapped) vkUnmapMemory(device, memory);
            if (buffer) vkDestroyBuffer(device, buffer, allocator);
            if (memory) vkFreeMemory(device, memory, allocator);
        }
    } upload_guard{v->Device, v->Allocator, upload_buffer_memory, upload_buffer};]=]
    _aeg_imgui_vulkan_source "${_aeg_imgui_vulkan_source}")
string(REPLACE [=[        err = vkMapMemory(v->Device, upload_buffer_memory, 0, upload_size, 0, (void**)(&map));
        check_vk_result(err);
        memcpy(map, pixels, upload_size);]=] [=[        err = vkMapMemory(v->Device, upload_buffer_memory, 0, upload_size, 0, (void**)(&map));
        check_vk_result(err);
        upload_guard.mapped = true;
        memcpy(map, pixels, upload_size);]=]
    _aeg_imgui_vulkan_source "${_aeg_imgui_vulkan_source}")
string(REPLACE [=[        vkUnmapMemory(v->Device, upload_buffer_memory);]=]
    [=[        vkUnmapMemory(v->Device, upload_buffer_memory);
        upload_guard.mapped = false;]=]
    _aeg_imgui_vulkan_source "${_aeg_imgui_vulkan_source}")
string(REPLACE [=[    vkDestroyBuffer(v->Device, upload_buffer, v->Allocator);
    vkFreeMemory(v->Device, upload_buffer_memory, v->Allocator);]=] [=[    // FontUploadGuard releases staging after the completed upload.]=]
    _aeg_imgui_vulkan_source "${_aeg_imgui_vulkan_source}")

string(SHA256 _aeg_imgui_vulkan_updated_hash "${_aeg_imgui_vulkan_source}")
if(NOT _aeg_imgui_vulkan_updated_hash STREQUAL "66fda3e62abfbb644759408283744a5783ffcef2156a9c82877ce0f91d5f9381")
    message(FATAL_ERROR "AEG ImGui font staging patch did not produce the expected source")
endif()
file(WRITE "${_aeg_imgui_vulkan_path}" "${_aeg_imgui_vulkan_source}")
unset(_aeg_imgui_vulkan_path)
unset(_aeg_imgui_vulkan_hash)
unset(_aeg_imgui_vulkan_source)
unset(_aeg_imgui_vulkan_updated_hash)
