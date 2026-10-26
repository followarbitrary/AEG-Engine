cmake_minimum_required(VERSION 3.25)
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    project(AEGMath LANGUAGES CXX)
    set(CMAKE_CXX_STANDARD_REQUIRED ON)
    set(CMAKE_CXX_EXTENSIONS OFF)
    include(CTest)
endif()

set(aeg_math_sources
    public/math.hpp
    private/vector.cpp
    private/quaternion.cpp
    private/matrix.cpp
    private/inverse.cpp
    private/matrix_validation.hpp
    private/projection.cpp
    private/frustum.cpp
    private/simd.cpp
    private/simd.hpp)

include("${CMAKE_CURRENT_SOURCE_DIR}/../../cmake/MathLibrary.cmake")

aeg_add_math_library(aeg_math FALSE)
add_library(aeg::math ALIAS aeg_math)
if(BUILD_TESTING)
    aeg_add_math_library(aeg_math_scalar TRUE)
    add_library(aeg::math_scalar ALIAS aeg_math_scalar)
endif()
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR AND BUILD_TESTING)
    add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/../../tests/math" "tests")
endif()
