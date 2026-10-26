add_library(aeg_gui INTERFACE)
add_library(aeg::gui ALIAS aeg_gui)
target_include_directories(aeg_gui INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}/public")
target_link_libraries(aeg_gui INTERFACE aeg::rhi aeg_imgui_core)
