add_executable(aeg_embedded_shader_tests EmbeddedShaderTests.cpp)
target_link_libraries(aeg_embedded_shader_tests PRIVATE aeg::render)
target_include_directories(aeg_embedded_shader_tests PRIVATE "${PROJECT_SOURCE_DIR}/engine/render/private/shaders")
aeg_project_options(aeg_embedded_shader_tests)
add_test(NAME aeg_embedded_shaders COMMAND aeg_embedded_shader_tests "${CMAKE_BINARY_DIR}/compiled-shaders")
set_tests_properties(aeg_embedded_shaders PROPERTIES LABELS cpu)

add_test(NAME aeg_spirv_generator COMMAND "${CMAKE_COMMAND}"
    "-DGENERATOR=${PROJECT_SOURCE_DIR}/cmake/EmbedSpirv.cmake"
    "-DWORK_DIR=${CMAKE_BINARY_DIR}/spirv-generator-tests"
    -P "${PROJECT_SOURCE_DIR}/cmake/EmbedSpirvMalformed.cmake")
set_tests_properties(aeg_spirv_generator PROPERTIES LABELS cpu TIMEOUT 60)
