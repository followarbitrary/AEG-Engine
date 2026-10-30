add_library(aeg_ecs STATIC private/EcsWorld.cpp public/EcsWorld.hpp)
add_library(aeg::ecs ALIAS aeg_ecs)
target_include_directories(aeg_ecs
    PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/public"
    PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/private")
target_link_libraries(aeg_ecs PUBLIC aeg::entt)
aeg_project_options(aeg_ecs)
