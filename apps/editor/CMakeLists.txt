add_executable(aeg_editor
    private/main.cpp
    private/GraphicsBackend.cpp
    private/CommandLine.cpp)
target_include_directories(aeg_editor PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/private")
target_link_libraries(aeg_editor PRIVATE aeg::editor aeg::foundation aeg::rhi_vulkan aeg::rhi_vulkan_gui)
if(WIN32)
    target_link_libraries(aeg_editor PRIVATE shell32)
endif()
aeg_project_options(aeg_editor)

# Always refresh runtime resources, including when no C++ file changed.
add_custom_target(aeg_resources ALL
    COMMAND "${CMAKE_COMMAND}" -E copy_directory "${PROJECT_SOURCE_DIR}/assets" "$<TARGET_FILE_DIR:aeg_editor>/assets"
    VERBATIM)
add_dependencies(aeg_editor aeg_resources)
