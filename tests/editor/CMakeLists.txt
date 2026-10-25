add_executable(aeg_editor_font_tests EditorFontTests.cpp)
# Font behavior is intentionally tested through this narrow private interface.
target_include_directories(aeg_editor_font_tests PRIVATE "${PROJECT_SOURCE_DIR}/editor/private/fonts")
target_link_libraries(aeg_editor_font_tests PRIVATE aeg::editor aeg_imgui)
aeg_project_options(aeg_editor_font_tests)
add_test(NAME aeg_editor_font_tests COMMAND aeg_editor_font_tests)
set_tests_properties(aeg_editor_font_tests PROPERTIES LABELS cpu TIMEOUT 60)
