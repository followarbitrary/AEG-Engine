add_library(aeg_render STATIC
    public/Renderer.hpp
    public/RenderSettings.hpp
    public/AntialiasingSettings.hpp
    public/Light.hpp
    public/ShadowSettings.hpp
    public/EnvironmentSettings.hpp
    public/ToneMappingSettings.hpp
    public/RenderGraph.hpp
    private/Renderer.cpp
    private/tonemapping/ToneMappingPass.cpp
    private/environment/EnvironmentBaker.cpp
    private/scene/GpuModel.cpp
    private/scene/ScenePass.cpp
    private/scene/ScenePipeline.cpp
    private/shaders/ShaderLibrary.cpp
    private/shadow/ShadowPass.cpp
    private/shadow/ShadowFitting.cpp
    private/viewport/ViewportTargets.cpp
    private/viewport/ViewportCapture.cpp
    private/antialiasing/Antialiasing.cpp
    private/antialiasing/TaaPass.cpp
    private/antialiasing/FxaaPass.cpp
    private/rendergraph/RenderGraph.cpp)
add_library(aeg::render ALIAS aeg_render)
target_include_directories(aeg_render
    PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/public"
    PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/private")
target_link_libraries(aeg_render
    PUBLIC aeg::assets aeg::camera aeg::rhi)
aeg_project_options(aeg_render)
