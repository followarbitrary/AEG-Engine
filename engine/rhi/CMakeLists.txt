add_library(aeg_rhi STATIC
    public/RhiUpload.hpp
    private/RhiValidation.cpp
    private/RhiDevice.cpp)
add_library(aeg::rhi ALIAS aeg_rhi)
target_include_directories(aeg_rhi PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/public")
target_compile_features(aeg_rhi PUBLIC cxx_std_23)
