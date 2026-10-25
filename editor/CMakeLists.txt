add_library(aeg_editor_module STATIC
    public/Editor.hpp
    private/Editor.cpp
    private/EditorWindow.cpp
    private/EditorStyle.cpp
    private/EditorDocking.cpp
    private/panels/ScenePanel.cpp
    private/panels/ImportPanel.cpp
    private/panels/InspectorPanel.cpp
    private/panels/ViewportPanel.cpp
    private/fonts/EditorFont.cpp)
add_library(aeg::editor ALIAS aeg_editor_module)
target_include_directories(aeg_editor_module
    PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/public"
    PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/private")
target_link_libraries(aeg_editor_module
    PRIVATE aeg::foundation aeg::camera aeg::assets aeg::render aeg_imgui glfw)
aeg_project_options(aeg_editor_module)
