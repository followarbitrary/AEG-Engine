add_executable(aeg_public_api_tests PublicApiTests.cpp)
# Use only exported module interfaces: no test-specific include directories.
target_link_libraries(aeg_public_api_tests PRIVATE
    aeg::foundation aeg::camera aeg::assets aeg::render aeg::editor)
aeg_project_options(aeg_public_api_tests)
add_test(NAME aeg_public_api_tests COMMAND aeg_public_api_tests)
set_tests_properties(aeg_public_api_tests PROPERTIES LABELS cpu)
