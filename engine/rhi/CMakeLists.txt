add_library(aeg_rhi STATIC private/RhiValidation.cpp)
add_library(aeg::rhi ALIAS aeg_rhi)
target_include_directories(aeg_rhi PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/public")
target_compile_features(aeg_rhi PUBLIC cxx_std_23)
