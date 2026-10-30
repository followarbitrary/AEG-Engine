add_subdirectory(math)
add_subdirectory(foundation)
add_subdirectory(camera)
add_subdirectory(ecs)
add_subdirectory(assets)
add_subdirectory(editor)
add_subdirectory(rhi)
add_subdirectory(shaders)

# Preserve the build helper target while keeping module tests independent.
add_custom_target(aeg_core_tests DEPENDS aeg_math_tests_normal aeg_math_tests_scalar aeg_camera_tests aeg_ecs_tests aeg_assets_tests
    aeg_editor_ecs_state_tests aeg_material_alpha_tests aeg_material_pbr_tests
    aeg_normal_mapping_assets_tests aeg_auto_normal_mapping_assets_tests aeg_mmd_effect_normal_tests)

if(AEG_ENABLE_GPU_TESTS)
    add_subdirectory(render)
endif()
