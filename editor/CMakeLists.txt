add_library(aeg_editor_module STATIC
    public/Editor.hpp
    public/EditorBackend.hpp
    private/Editor.cpp
    private/EditorWindow.cpp
    private/EditorStyle.cpp
    private/EditorDocking.cpp
    private/graphics/EditorGraphics.cpp
    private/platform/NativeWindow.cpp
    private/panels/ScenePanel.cpp
    private/panels/ImportPanel.cpp
    private/panels/InspectorPanel.cpp
    private/panels/ToneMappingPanel.cpp
    private/panels/MaterialPanel.cpp
    private/panels/LightPanel.cpp
    private/panels/EnvironmentPanel.cpp
    private/panels/ViewportPanel.cpp
    private/panels/DebugPanel.cpp
    private/gizmo/Transform.cpp
    private/SceneHierarchy.cpp
    private/gizmo/ViewportGizmo.cpp
    private/gizmo/OrientationGizmo.cpp
    private/fonts/EditorFont.cpp)
add_library(aeg::editor ALIAS aeg_editor_module)
target_include_directories(aeg_editor_module
    PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/public"
    PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/private"
            "${PROJECT_SOURCE_DIR}/engine/render/private/rendergraph")
target_link_libraries(aeg_editor_module
    PUBLIC aeg::render aeg::gui
    PRIVATE aeg::foundation aeg::camera aeg::assets aeg::ecs aeg_imgui_glfw aeg_imguizmo glfw)
aeg_project_options(aeg_editor_module)
