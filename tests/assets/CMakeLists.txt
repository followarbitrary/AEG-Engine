add_executable(aeg_material_alpha_tests MaterialAlphaTests.cpp)
target_link_libraries(aeg_material_alpha_tests PRIVATE aeg::assets)
target_compile_definitions(aeg_material_alpha_tests PRIVATE AEG_TEST_FIXTURES="${PROJECT_SOURCE_DIR}/tests/fixtures")
aeg_project_options(aeg_material_alpha_tests)
add_test(NAME aeg_material_alpha_tests COMMAND aeg_material_alpha_tests)
set_tests_properties(aeg_material_alpha_tests PROPERTIES LABELS cpu TIMEOUT 60)

add_executable(aeg_assets_tests ModelImporterTests.cpp)
target_link_libraries(aeg_assets_tests PRIVATE aeg::assets)
target_compile_definitions(aeg_assets_tests PRIVATE
    AEG_SOURCE_DIR="${PROJECT_SOURCE_DIR}"
    AEG_TEST_FIXTURES="${PROJECT_SOURCE_DIR}/tests/fixtures")
aeg_project_options(aeg_assets_tests)
add_test(NAME aeg_assets_tests COMMAND aeg_assets_tests)
set_tests_properties(aeg_assets_tests PROPERTIES
    WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}" LABELS cpu)

add_executable(aeg_normal_mapping_assets_tests NormalMappingTests.cpp)
target_link_libraries(aeg_normal_mapping_assets_tests PRIVATE aeg::assets)
target_compile_definitions(aeg_normal_mapping_assets_tests PRIVATE AEG_SOURCE_DIR="${PROJECT_SOURCE_DIR}")
aeg_project_options(aeg_normal_mapping_assets_tests)
add_test(NAME aeg_normal_mapping_assets_tests COMMAND aeg_normal_mapping_assets_tests)
set_tests_properties(aeg_normal_mapping_assets_tests PROPERTIES LABELS cpu TIMEOUT 60)

add_executable(aeg_auto_normal_mapping_assets_tests AutoNormalMappingTests.cpp)
target_link_libraries(aeg_auto_normal_mapping_assets_tests PRIVATE aeg::assets)
target_compile_definitions(aeg_auto_normal_mapping_assets_tests PRIVATE
    AEG_TEST_FIXTURES="${PROJECT_SOURCE_DIR}/tests/fixtures")
aeg_project_options(aeg_auto_normal_mapping_assets_tests)
add_test(NAME aeg_auto_normal_mapping_assets_tests COMMAND aeg_auto_normal_mapping_assets_tests)
set_tests_properties(aeg_auto_normal_mapping_assets_tests PROPERTIES LABELS cpu TIMEOUT 60)

add_executable(aeg_mmd_effect_normal_tests MmdEffectNormalTests.cpp)
target_link_libraries(aeg_mmd_effect_normal_tests PRIVATE aeg::assets)
target_compile_definitions(aeg_mmd_effect_normal_tests PRIVATE
    AEG_TEST_FIXTURES="${PROJECT_SOURCE_DIR}/tests/fixtures")
aeg_project_options(aeg_mmd_effect_normal_tests)
add_test(NAME aeg_mmd_effect_normal_tests COMMAND aeg_mmd_effect_normal_tests)
set_tests_properties(aeg_mmd_effect_normal_tests PROPERTIES LABELS cpu TIMEOUT 60)
