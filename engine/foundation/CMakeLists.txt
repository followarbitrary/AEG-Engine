add_library(aeg_foundation STATIC
    public/Bounds.hpp
    public/Utf8Path.hpp
    private/Utf8Path.cpp)
add_library(aeg::foundation ALIAS aeg_foundation)
target_include_directories(aeg_foundation
    PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/public"
    PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/private")
target_link_libraries(aeg_foundation PUBLIC glm::glm)
aeg_project_options(aeg_foundation)
