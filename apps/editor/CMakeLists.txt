add_executable(aeg_editor
    private/main.cpp
    private/CommandLine.cpp)
target_include_directories(aeg_editor PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/private")
target_link_libraries(aeg_editor PRIVATE aeg::editor aeg::foundation)
if(WIN32)
    target_link_libraries(aeg_editor PRIVATE shell32)
endif()
aeg_project_options(aeg_editor)
add_dependencies(aeg_editor aeg_shaders)

# Always refresh runtime resources, including when no C++ file changed.
add_custom_target(aeg_resources ALL
    COMMAND "${CMAKE_COMMAND}" -E make_directory "$<TARGET_FILE_DIR:aeg_editor>/shaders"
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different ${aeg_shader_outputs} "$<TARGET_FILE_DIR:aeg_editor>/shaders"
    COMMAND "${CMAKE_COMMAND}" -E copy_directory "${PROJECT_SOURCE_DIR}/assets" "$<TARGET_FILE_DIR:aeg_editor>/assets"
    DEPENDS aeg_shaders VERBATIM)
add_dependencies(aeg_editor aeg_resources)
