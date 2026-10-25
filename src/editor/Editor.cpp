#include <aeg/Editor.hpp>
#include <aeg/Model.hpp>
#include <aeg/OrbitCamera.hpp>
#include <aeg/VulkanRenderer.hpp>

#include <GLFW/glfw3.h>
#include <imgui.h>
#include <imgui_internal.h>
#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <deque>
#include <exception>
#include <iostream>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>

namespace aeg {
namespace {
std::string pathText(const std::filesystem::path& path) {
    const auto value = path.u8string();
    return {reinterpret_cast<const char*>(value.data()), value.size()};
}

std::filesystem::path pathFromText(std::string_view value) {
    return std::filesystem::path(std::u8string(value.begin(), value.end()));
}

struct WindowLifetime {
    bool initialized{false};
    GLFWwindow* window{nullptr};
    ImGuiContext* context{nullptr};
    ~WindowLifetime() {
        if (context) ImGui::DestroyContext(context);
        if (window) glfwDestroyWindow(window);
        if (initialized) glfwTerminate();
    }
};

struct ImportQueue {
    std::deque<std::filesystem::path> paths;
    std::string error;
};

struct Transform {
    glm::vec3 translation{0};
    glm::vec3 rotation{0};
    glm::vec3 scale{1};
    glm::mat4 matrix() const {
        auto result = glm::translate(glm::mat4(1), translation);
        result = glm::rotate(result, glm::radians(rotation.x), glm::vec3(1, 0, 0));
        result = glm::rotate(result, glm::radians(rotation.y), glm::vec3(0, 1, 0));
        result = glm::rotate(result, glm::radians(rotation.z), glm::vec3(0, 0, 1));
        return glm::scale(result, scale);
    }
};

Bounds transformedBounds(const Bounds& bounds, const glm::mat4& transform) {
    Bounds result;
    result.min = glm::vec3(std::numeric_limits<float>::max());
    result.max = glm::vec3(std::numeric_limits<float>::lowest());
    for (unsigned corner = 0; corner < 8; ++corner) {
        const glm::vec3 point{corner & 1 ? bounds.max.x : bounds.min.x, corner & 2 ? bounds.max.y : bounds.min.y,
                              corner & 4 ? bounds.max.z : bounds.min.z};
        const auto world = glm::vec3(transform * glm::vec4(point, 1));
        result.min = glm::min(result.min, world);
        result.max = glm::max(result.max, world);
    }
    return result;
}

void configureStyle() {
    ImGui::StyleColorsDark();
    auto& style = ImGui::GetStyle();
    style.WindowPadding = {14, 12};
    style.FramePadding = {8, 6};
    style.ItemSpacing = {8, 8};
    style.WindowRounding = 0;
    style.ChildRounding = 5;
    style.FrameRounding = 4;
    style.PopupRounding = 5;
    style.GrabRounding = 3;
    style.TabRounding = 4;
    style.WindowBorderSize = 0;
    style.FrameBorderSize = 0;
    style.Colors[ImGuiCol_WindowBg] = {0.067f, 0.078f, 0.098f, 1};
    style.Colors[ImGuiCol_ChildBg] = {0.055f, 0.063f, 0.080f, 1};
    style.Colors[ImGuiCol_TitleBg] = {0.045f, 0.055f, 0.070f, 1};
    style.Colors[ImGuiCol_TitleBgActive] = {0.09f, 0.12f, 0.16f, 1};
    style.Colors[ImGuiCol_Header] = {0.12f, 0.24f, 0.32f, 1};
    style.Colors[ImGuiCol_HeaderHovered] = {0.16f, 0.32f, 0.43f, 1};
    style.Colors[ImGuiCol_Button] = {0.13f, 0.28f, 0.38f, 1};
    style.Colors[ImGuiCol_ButtonHovered] = {0.18f, 0.40f, 0.54f, 1};
    style.Colors[ImGuiCol_CheckMark] = {0.33f, 0.77f, 0.94f, 1};
    style.Colors[ImGuiCol_SliderGrab] = {0.27f, 0.65f, 0.83f, 1};
    style.Colors[ImGuiCol_DockingPreview] = {0.25f, 0.60f, 0.85f, 0.5f};
}

void dockLayout(ImGuiID dockId) {
    if (ImGui::DockBuilderGetNode(dockId)) return;
    ImGui::DockBuilderAddNode(dockId, ImGuiDockNodeFlags_DockSpace);
    ImGui::DockBuilderSetNodeSize(dockId, ImGui::GetMainViewport()->WorkSize);
    auto center = dockId;
    const auto inspector = ImGui::DockBuilderSplitNode(center, ImGuiDir_Right, 0.25f, nullptr, &center);
    auto left = ImGui::DockBuilderSplitNode(center, ImGuiDir_Left, 0.23f, nullptr, &center);
    const auto assets = ImGui::DockBuilderSplitNode(left, ImGuiDir_Down, 0.57f, nullptr, &left);
    ImGui::DockBuilderDockWindow("Viewport", center);
    ImGui::DockBuilderDockWindow("Scene", left);
    ImGui::DockBuilderDockWindow("Import", assets);
    ImGui::DockBuilderDockWindow("Inspector", inspector);
    ImGui::DockBuilderFinish(dockId);
}
}

int runEditor(const EditorOptions& options) {
    WindowLifetime lifetime;
    glfwSetErrorCallback([](int code, const char* text) { std::cerr << "GLFW " << code << ": " << text << '\n'; });
    if (!glfwInit()) throw std::runtime_error("GLFW initialization failed.");
    lifetime.initialized = true;
    if (!glfwVulkanSupported()) throw std::runtime_error("No Vulkan loader or compatible driver was found.");
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    glfwWindowHint(GLFW_VISIBLE, options.hidden ? GLFW_FALSE : GLFW_TRUE);
    glfwWindowHint(GLFW_FOCUSED, options.hidden ? GLFW_FALSE : GLFW_TRUE);
    lifetime.window = glfwCreateWindow(1280, 800, "AEG Engine | Model Studio", nullptr, nullptr);
    if (!lifetime.window) throw std::runtime_error("Cannot create the editor window.");
    IMGUI_CHECKVERSION();
    lifetime.context = ImGui::CreateContext();
    auto& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable | ImGuiConfigFlags_NavEnableKeyboard;
    io.IniFilename = nullptr;
    configureStyle();

    // This scope makes backend teardown precede the ImGui context and native window.
    VulkanRenderer renderer(lifetime.window, options.resourceDirectory / "shaders", options.validation);
    std::cout << "GPU: " << renderer.deviceName() << '\n';
    ModelData model =
        loadModel(options.initialModel.empty() ? options.resourceDirectory / "assets" / "models" / "demo.obj"
                                               : options.initialModel);
    renderer.uploadModel(model);
    std::cout << "Model: " << pathText(model.source) << " (" << model.vertexCount() << " vertices, "
              << model.triangleCount() << " triangles)\n";
    OrbitCamera camera;
    camera.fit(model.bounds);
    Transform transform;
    RenderSettings settings;
    ImportQueue imports;
    std::array<char, 4096> importPath{};
    const auto initialPath = pathText(model.source);
    std::snprintf(importPath.data(), importPath.size(), "%s", initialPath.c_str());
    glfwSetWindowUserPointer(lifetime.window, &imports);
    glfwSetDropCallback(lifetime.window, [](GLFWwindow* window, int count, const char** paths) {
        auto& queue = *static_cast<ImportQueue*>(glfwGetWindowUserPointer(window));
        try {
            for (int i = 0; i < count; ++i) queue.paths.push_back(pathFromText(paths[i]));
        } catch (const std::exception& error) {
            queue.error = error.what();
        }
    });
    bool autoRotate = false;
    bool frameRequested = false;
    bool sceneRendered = false;
    std::array<bool, 3> viewportDrag{};
    std::uint32_t frames = 0;
    std::uint32_t viewportWidth = 1, viewportHeight = 1;
    std::string status = "Ready. Drop a model anywhere to import.";
    double previousTime = glfwGetTime();

    while (!glfwWindowShouldClose(lifetime.window) && (options.frameLimit == 0 || frames < options.frameLimit)) {
        glfwPollEvents();
        int framebufferWidth = 0, framebufferHeight = 0;
        glfwGetFramebufferSize(lifetime.window, &framebufferWidth, &framebufferHeight);
        if (framebufferWidth == 0 || framebufferHeight == 0) {
            glfwWaitEventsTimeout(0.05);
            previousTime = glfwGetTime();
            continue;
        }
        while (!imports.paths.empty()) {
            const auto path = std::move(imports.paths.front());
            imports.paths.pop_front();
            try {
                auto candidate = loadModel(path);
                renderer.uploadModel(candidate);
                model = std::move(candidate);
                transform = {};
                camera.fit(model.bounds);
                imports.error.clear();
                status = "Loaded " + model.name;
                const auto text = pathText(path);
                std::snprintf(importPath.data(), importPath.size(), "%s", text.c_str());
                std::cout << "Model: " << text << " (" << model.vertexCount() << " vertices, " << model.triangleCount()
                          << " triangles)\n";
            } catch (const std::exception& error) {
                imports.error = error.what();
                status = "Import failed. The current model is retained.";
                std::cerr << "Import error: " << error.what() << '\n';
            }
        }
        const double now = glfwGetTime();
        const auto elapsed = static_cast<float>(std::clamp(now - previousTime, 0.0, 0.1));
        previousTime = now;
        if (autoRotate) transform.rotation.y = std::fmod(transform.rotation.y + elapsed * 22.0f, 360.0f);
        bool builtUi = false;
        renderer.drawFrame(settings, [&] {
            builtUi = true;
            const auto* viewport = ImGui::GetMainViewport();
            ImGui::SetNextWindowPos(viewport->WorkPos);
            ImGui::SetNextWindowSize(viewport->WorkSize);
            ImGui::SetNextWindowViewport(viewport->ID);
            ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
            ImGui::Begin("AEG Workspace", nullptr,
                         ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize |
                             ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoBringToFrontOnFocus |
                             ImGuiWindowFlags_NoNavFocus | ImGuiWindowFlags_MenuBar | ImGuiWindowFlags_NoDocking);
            ImGui::PopStyleVar();
            if (ImGui::BeginMenuBar()) {
                ImGui::TextColored({0.40f, 0.79f, 0.95f, 1}, "AEG");
                ImGui::TextUnformatted(" / MODEL STUDIO");
                ImGui::Separator();
                ImGui::Text("%.0f FPS  |  %.2f ms", io.Framerate, io.Framerate > 0 ? 1000.0f / io.Framerate : 0.0f);
                ImGui::Separator();
                ImGui::TextUnformatted(renderer.deviceName().c_str());
                ImGui::EndMenuBar();
            }
            const auto dockId = ImGui::GetID("AEGDockSpace");
            dockLayout(dockId);
            ImGui::DockSpace(dockId);
            ImGui::End();

            if (ImGui::Begin("Scene")) {
                ImGui::TextDisabled("ACTIVE MODEL");
                ImGui::Selectable(model.name.c_str(), true);
                ImGui::Separator();
                ImGui::Text("%zu meshes", model.meshes.size());
                ImGui::Text("%zu materials", model.materials.size());
                ImGui::Text("%zu vertices", model.vertexCount());
                ImGui::Text("%zu triangles", model.triangleCount());
                if (ImGui::Button("Frame model", {-1, 0})) frameRequested = true;
                ImGui::Spacing();
                ImGui::TextDisabled("MESHES");
                if (ImGui::BeginTable("MeshList", 2, ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp)) {
                    ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthStretch);
                    ImGui::TableSetupColumn("Triangles", ImGuiTableColumnFlags_WidthFixed, 72);
                    ImGui::TableHeadersRow();
                    for (std::size_t i = 0; i < model.meshes.size(); ++i) {
                        const auto& mesh = model.meshes[i];
                        ImGui::TableNextRow();
                        ImGui::TableNextColumn();
                        ImGui::TextUnformatted(mesh.name.empty() ? "Mesh" : mesh.name.c_str());
                        ImGui::TableNextColumn();
                        ImGui::Text("%zu", mesh.indices.size() / 3);
                    }
                    ImGui::EndTable();
                }
            }
            ImGui::End();

            if (ImGui::Begin("Import")) {
                ImGui::TextDisabled("MODEL LIBRARY");
                ImGui::TextWrapped("Open an OBJ, glTF, GLB or PMX file, or drop one onto the window.");
                ImGui::SetNextItemWidth(-1);
                const bool enter = ImGui::InputTextWithHint("##ModelPath", "Model file path", importPath.data(),
                                                            importPath.size(), ImGuiInputTextFlags_EnterReturnsTrue);
                if (ImGui::Button("Import model", {-1, 0}) || enter) {
                    if (importPath[0])
                        imports.paths.push_back(pathFromText(importPath.data()));
                    else
                        imports.error = "Enter a model file path first.";
                }
                ImGui::Separator();
                ImGui::TextWrapped("%s", status.c_str());
                if (!imports.error.empty()) {
                    ImGui::PushStyleColor(ImGuiCol_Text, {1, 0.49f, 0.43f, 1});
                    ImGui::TextWrapped("%s", imports.error.c_str());
                    ImGui::PopStyleColor();
                }
                if (!model.warnings.empty() && ImGui::CollapsingHeader("Import notices"))
                    for (const auto& warning : model.warnings) ImGui::TextWrapped("%s", warning.c_str());
                ImGui::Spacing();
                ImGui::TextDisabled("VIEWPORT CONTROLS");
                ImGui::TextWrapped("Left / right drag: orbit\nMiddle drag: pan\nWheel: zoom\nF: frame model");
            }
            ImGui::End();

            if (ImGui::Begin("Inspector")) {
                if (ImGui::CollapsingHeader("Transform", ImGuiTreeNodeFlags_DefaultOpen)) {
                    ImGui::DragFloat3("Position", &transform.translation.x, 0.02f);
                    ImGui::DragFloat3("Rotation", &transform.rotation.x, 0.5f);
                    ImGui::DragFloat3("Scale", &transform.scale.x, 0.01f, -1000, 1000);
                    for (int axis = 0; axis < 3; ++axis) {
                        if (!std::isfinite(transform.translation[axis])) transform.translation[axis] = 0;
                        if (!std::isfinite(transform.rotation[axis])) transform.rotation[axis] = 0;
                        if (!std::isfinite(transform.scale[axis])) transform.scale[axis] = 1;
                        if (std::abs(transform.scale[axis]) < 0.001f)
                            transform.scale[axis] = std::signbit(transform.scale[axis]) ? -0.001f : 0.001f;
                    }
                    ImGui::Checkbox("Auto rotate", &autoRotate);
                    if (ImGui::Button("Reset transform")) {
                        transform = {};
                        frameRequested = true;
                    }
                }
                if (ImGui::CollapsingHeader("Lighting", ImGuiTreeNodeFlags_DefaultOpen)) {
                    ImGui::Checkbox("Unlit materials", &settings.unlit);
                    ImGui::SliderFloat("Ambient", &settings.ambient, 0, 1, "%.2f");
                    ImGui::TextUnformatted("Light direction");
                    ImGui::SetNextItemWidth(-1);
                    ImGui::SliderFloat3("##LightDirection", &settings.lightDirection.x, -1, 1, "%.2f");
                    if (glm::dot(settings.lightDirection, settings.lightDirection) < 0.0001f)
                        settings.lightDirection = {0, -1, 0};
                    ImGui::ColorEdit3("Background", &settings.background.x);
                }
                ImGui::Spacing();
                ImGui::TextDisabled("RENDERER");
                ImGui::TextWrapped("Vulkan | %s", renderer.deviceName().c_str());
                ImGui::Text("Validation errors: %u", renderer.validationErrorCount());
                ImGui::Text("Viewport: %u x %u", viewportWidth, viewportHeight);
            }
            ImGui::End();

            ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
            if (ImGui::Begin("Viewport", nullptr, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse)) {
                const auto available = ImGui::GetContentRegionAvail();
                if (available.x >= 1 && available.y >= 1) {
                    viewportWidth = static_cast<std::uint32_t>(
                        std::max(1.0f, std::floor(available.x * io.DisplayFramebufferScale.x)));
                    viewportHeight = static_cast<std::uint32_t>(
                        std::max(1.0f, std::floor(available.y * io.DisplayFramebufferScale.y)));
                    renderer.resizeViewport(viewportWidth, viewportHeight);
                    const auto imagePosition = ImGui::GetCursorScreenPos();
                    ImGui::Image(renderer.viewportTexture(), available);
                    // Image has no active ID. Own the gesture so ImGui's window-move
                    // ID cannot consume drags over the scene, while other widgets
                    // still block camera input.
                    ImGui::SetCursorScreenPos(imagePosition);
                    ImGui::InvisibleButton("##ViewportInput", available,
                                           ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight |
                                               ImGuiButtonFlags_MouseButtonMiddle);
                    const bool inputAvailable = !ImGui::IsAnyItemActive() || ImGui::IsItemActive();
                    for (int button = 0; button < 3; ++button) {
                        if (!ImGui::IsMouseDown(button)) viewportDrag[button] = false;
                        if (ImGui::IsItemHovered() && inputAvailable && ImGui::IsMouseClicked(button))
                            viewportDrag[button] = true;
                    }
                    if (ImGui::IsItemHovered() && inputAvailable && !io.WantTextInput) {
                        if ((viewportDrag[ImGuiMouseButton_Left] && ImGui::IsMouseDragging(ImGuiMouseButton_Left)) ||
                            (viewportDrag[ImGuiMouseButton_Right] && ImGui::IsMouseDragging(ImGuiMouseButton_Right)))
                            camera.orbit(io.MouseDelta.x, io.MouseDelta.y);
                        if (viewportDrag[ImGuiMouseButton_Middle] && ImGui::IsMouseDragging(ImGuiMouseButton_Middle))
                            camera.pan(io.MouseDelta.x, io.MouseDelta.y, available.y);
                        if (io.MouseWheel != 0) camera.zoom(io.MouseWheel);
                        if (ImGui::IsKeyPressed(ImGuiKey_F, false)) frameRequested = true;
                    }
                    sceneRendered = true;
                }
            }
            ImGui::End();
            ImGui::PopStyleVar();
            settings.model = transform.matrix();
            if (frameRequested) {
                camera.fit(transformedBounds(model.bounds, settings.model));
                frameRequested = false;
            }
            settings.viewProjection =
                camera.projection(static_cast<float>(viewportWidth) / static_cast<float>(viewportHeight)) *
                camera.view();
        });
        if (builtUi) ++frames;
    }
    renderer.waitIdle();
    if (!options.capturePath.empty()) {
        if (!sceneRendered) throw std::runtime_error("Cannot capture: the viewport has not rendered a scene.");
        renderer.captureViewport(options.capturePath);
        std::cout << "Capture: " << pathText(options.capturePath) << '\n';
    }
    const auto errors = renderer.validationErrorCount();
    std::cout << "Rendered frames: " << frames << "\nValidation errors: " << errors << '\n';
    glfwSetDropCallback(lifetime.window, nullptr);
    glfwSetWindowUserPointer(lifetime.window, nullptr);
    return errors == 0 ? 0 : 2;
}
}
