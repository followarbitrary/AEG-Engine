add_executable(aeg_editor_font_tests EditorFontTests.cpp)
# Font behavior is intentionally tested through this narrow private interface.
target_include_directories(aeg_editor_font_tests PRIVATE "${PROJECT_SOURCE_DIR}/editor/private/fonts")
target_link_libraries(aeg_editor_font_tests PRIVATE aeg::editor aeg_imgui_core)
aeg_project_options(aeg_editor_font_tests)
add_test(NAME aeg_editor_font_tests COMMAND aeg_editor_font_tests)
set_tests_properties(aeg_editor_font_tests PROPERTIES LABELS cpu TIMEOUT 60)

add_executable(aeg_transform_gizmo_tests TransformGizmoTests.cpp
    "${PROJECT_SOURCE_DIR}/editor/private/gizmo/Transform.cpp")
target_include_directories(aeg_transform_gizmo_tests PRIVATE "${PROJECT_SOURCE_DIR}/editor/private/gizmo")
target_link_libraries(aeg_transform_gizmo_tests PRIVATE aeg::math)
aeg_project_options(aeg_transform_gizmo_tests)
add_test(NAME aeg_transform_gizmo COMMAND aeg_transform_gizmo_tests)
set_tests_properties(aeg_transform_gizmo PROPERTIES LABELS cpu TIMEOUT 60)

add_executable(aeg_viewport_gizmo_tests ViewportGizmoTests.cpp)
target_include_directories(aeg_viewport_gizmo_tests PRIVATE "${PROJECT_SOURCE_DIR}/editor/private/gizmo")
target_link_libraries(aeg_viewport_gizmo_tests PRIVATE aeg::editor aeg::camera)
aeg_project_options(aeg_viewport_gizmo_tests)
add_test(NAME aeg_viewport_gizmo COMMAND aeg_viewport_gizmo_tests)
set_tests_properties(aeg_viewport_gizmo PROPERTIES LABELS cpu TIMEOUT 60)

add_executable(aeg_orientation_gizmo_tests OrientationGizmoTests.cpp)
target_include_directories(aeg_orientation_gizmo_tests PRIVATE "${PROJECT_SOURCE_DIR}/editor/private")
target_link_libraries(aeg_orientation_gizmo_tests PRIVATE aeg::editor aeg::camera)
aeg_project_options(aeg_orientation_gizmo_tests)
add_test(NAME aeg_orientation_gizmo COMMAND aeg_orientation_gizmo_tests)
set_tests_properties(aeg_orientation_gizmo PROPERTIES LABELS cpu TIMEOUT 60)
