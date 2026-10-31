#include <ModelResources.hpp>

#include <chrono>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <thread>

#ifndef AEG_SOURCE_DIR
#define AEG_SOURCE_DIR "."
#endif

namespace {
void require(bool value, const char* message) {
    if (!value) throw std::runtime_error(message);
}

std::vector<aeg::assets::ModelCompletion> waitFor(aeg::assets::ModelResources& resources,
                                                   aeg::assets::ModelHandle handle) {
    for (int attempt = 0; attempt < 500; ++attempt) {
        auto completions = resources.drainCompleted();
        for (const auto& completion : completions)
            if (completion.handle == handle) return completions;
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    throw std::runtime_error("resource load did not complete");
}

void cachesSuccessfulModels() {
    aeg::assets::ModelResources resources;
    const auto path = std::filesystem::path(AEG_SOURCE_DIR) / "tests/fixtures/triangle.obj";
    const auto first = resources.request(path);
    const auto second = resources.request(path.lexically_normal());
    require(first == second, "same model path did not reuse its handle");
    const auto completions = waitFor(resources, first);
    bool ready = false;
    for (const auto& completion : completions)
        ready |= completion.handle == first && completion.state == aeg::assets::ModelLoadState::Ready &&
                 completion.data && completion.data->triangleCount() == 1;
    require(ready, "background model load did not publish a ready immutable model");
    require(resources.state(first) == aeg::assets::ModelLoadState::Ready, "ready state was not retained in cache");
}

void reportsFailures() {
    aeg::assets::ModelResources resources;
    const auto handle = resources.request(std::filesystem::path(AEG_SOURCE_DIR) / "tests/fixtures/absent.obj");
    const auto completions = waitFor(resources, handle);
    bool failed = false;
    for (const auto& completion : completions)
        failed |= completion.handle == handle && completion.state == aeg::assets::ModelLoadState::Failed &&
                  !completion.error.empty();
    require(failed && resources.state(handle) == aeg::assets::ModelLoadState::Failed,
            "background model failure did not reach the completion queue");
}
}

int main() {
    try {
        cachesSuccessfulModels();
        reportsFailures();
        std::cout << "PASS model resources\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "FAIL model resources: " << error.what() << '\n';
        return 1;
    }
}
