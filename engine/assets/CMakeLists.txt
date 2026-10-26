add_library(aeg_assets STATIC
    public/ModelData.hpp
    public/ModelImporter.hpp
    public/MaterialTextures.hpp
    public/MaterialProperties.hpp
    private/ModelData.cpp
    private/ModelImporter.cpp
    private/material/MaterialTextures.cpp
    private/material/MaterialProperties.cpp
    private/material/ImportedAlpha.cpp
    private/material/NormalMapDiscovery.cpp
    private/material/effects/EffectNormalTexture.cpp
    private/mmd/MmdEffectNormals.cpp
    private/geometry/TangentSpace.cpp
    private/assimp/AssimpFileSystem.cpp
    private/image/ImageDecoder.cpp
    private/pmx/PmxValidation.cpp)
add_library(aeg::assets ALIAS aeg_assets)
target_include_directories(aeg_assets
    PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/public"
    PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/private")
target_include_directories(aeg_assets SYSTEM PRIVATE "${stb_SOURCE_DIR}")
target_link_libraries(aeg_assets PUBLIC aeg::foundation PRIVATE assimp::assimp)
aeg_project_options(aeg_assets)
