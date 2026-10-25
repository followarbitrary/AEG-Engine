add_library(aeg_camera STATIC
    public/OrbitCamera.hpp
    private/OrbitCamera.cpp)
add_library(aeg::camera ALIAS aeg_camera)
target_include_directories(aeg_camera
    PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/public"
    PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/private")
target_link_libraries(aeg_camera PUBLIC aeg::foundation)
aeg_project_options(aeg_camera)
