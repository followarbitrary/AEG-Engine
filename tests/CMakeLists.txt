add_subdirectory(foundation)
add_subdirectory(camera)
add_subdirectory(assets)
add_subdirectory(editor)

# Preserve the build helper target while keeping module tests independent.
add_custom_target(aeg_core_tests DEPENDS aeg_camera_tests aeg_assets_tests)

if(AEG_ENABLE_GPU_TESTS)
    add_subdirectory(render)
endif()
