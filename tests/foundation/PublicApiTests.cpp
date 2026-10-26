#include <Bounds.hpp>
#include <Utf8Path.hpp>
#include <OrbitCamera.hpp>
#include <ModelData.hpp>
#include <ModelImporter.hpp>
#include <MaterialTextures.hpp>
#include <RenderSettings.hpp>
#include <AntialiasingSettings.hpp>
#include <Renderer.hpp>
#include <EditorBackend.hpp>
#include <Editor.hpp>

#include <cmath>
#include <filesystem>
#include <functional>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>

namespace {
static_assert(std::is_same_v<decltype(aeg::assets::ModelData{}.bounds), aeg::foundation::Bounds>);
static_assert(
    std::is_same_v<decltype(&aeg::assets::loadModel), aeg::assets::ModelData (*)(const std::filesystem::path&)>);
static_assert(std::is_same_v<decltype(&aeg::assets::prepareModelMaterials), void (*)(aeg::assets::ModelData&)>);
static_assert(std::is_same_v<decltype(&aeg::assets::initializeModelMaterials), void (*)(aeg::assets::ModelData&)>);
static_assert(
    std::is_same_v<decltype(&aeg::assets::clearMaterialNormalMap), void (*)(aeg::assets::ModelData&, std::size_t)>);
static_assert(std::is_same_v<decltype(&aeg::foundation::pathToUtf8), std::string (*)(const std::filesystem::path&)>);
static_assert(std::is_same_v<decltype(&aeg::foundation::pathFromUtf8), std::filesystem::path (*)(std::string_view)>);
static_assert(std::is_same_v<decltype(&aeg::camera::OrbitCamera::fit),
                             void (aeg::camera::OrbitCamera::*)(const aeg::foundation::Bounds&)>);
static_assert(std::is_same_v<decltype(&aeg::render::Renderer::uploadModel),
                             void (aeg::render::Renderer::*)(const aeg::assets::ModelData&)>);
static_assert(std::is_same_v<decltype(&aeg::render::Renderer::setAntialiasing),
                             void (aeg::render::Renderer::*)(const aeg::render::AntialiasingSettings&)>);
static_assert(
    std::is_same_v<decltype(&aeg::render::Renderer::recordFrame),
                   void (aeg::render::Renderer::*)(aeg::rhi::CommandList&, const aeg::render::RenderSettings&)>);
static_assert(std::is_same_v<decltype(&aeg::editor::runEditor),
                             int (*)(const aeg::editor::EditorOptions&, const aeg::editor::EditorBackendFactory&)>);
static_assert(!std::is_copy_constructible_v<aeg::render::Renderer>);

void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

void publicCpuContracts() {
    const auto path = std::filesystem::path(u8"models/\u6a21\u578b with spaces+%20.pmx");
    const auto encoded = aeg::foundation::pathToUtf8(path);
    require(aeg::foundation::pathFromUtf8(encoded) == path, "UTF-8 path round trip");
    require(encoded.find("with spaces+%20.pmx") != std::string::npos,
            "path encoding preserves spaces, plus signs and literal percent escapes");

    const aeg::foundation::Bounds bounds{{-2, -3, -6}, {2, 3, 6}};
    require(glm::length(bounds.center()) < 0.001f, "bounds center");
    require(std::abs(bounds.radius() - 7.0f) < 0.001f, "bounds radius");
    require(aeg::foundation::Bounds{}.radius() >= 0.01f, "degenerate bounds radius floor");

    aeg::camera::OrbitCamera camera;
    camera.fit(bounds);
    require(glm::length(camera.target() - bounds.center()) < 0.001f, "camera fits shared bounds");
    require(std::isfinite(camera.distance()) && camera.distance() > bounds.radius(), "camera fit distance");
    const auto view = camera.view();
    const auto projection = camera.projection(16.0f / 9.0f);
    for (int column = 0; column < 4; ++column) {
        for (int row = 0; row < 4; ++row) {
            require(std::isfinite(view[column][row]) && std::isfinite(projection[column][row]),
                    "camera matrices remain finite");
        }
    }
}
}

int main() {
    try {
        publicCpuContracts();
        std::cout << "PASS exported module public API\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "FAIL exported module public API: " << error.what() << '\n';
        return 1;
    }
}
