add_executable(aeg_assets_tests ModelImporterTests.cpp)
target_link_libraries(aeg_assets_tests PRIVATE aeg::assets)
target_compile_definitions(aeg_assets_tests PRIVATE
    AEG_SOURCE_DIR="${PROJECT_SOURCE_DIR}"
    AEG_TEST_FIXTURES="${PROJECT_SOURCE_DIR}/tests/fixtures")
aeg_project_options(aeg_assets_tests)
add_test(NAME aeg_assets_tests COMMAND aeg_assets_tests)
set_tests_properties(aeg_assets_tests PROPERTIES
    WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}" LABELS cpu)
