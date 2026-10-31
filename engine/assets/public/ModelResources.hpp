#pragma once

#include <ModelData.hpp>

#include <cstdint>
#include <filesystem>
#include <memory>
#include <vector>

namespace aeg::assets {

using ModelHandle = std::uint64_t;

enum class ModelLoadState : std::uint8_t { Invalid, Queued, Loading, Ready, Failed };

struct ModelCompletion {
    ModelHandle handle{};
    std::filesystem::path path;
    ModelLoadState state{ModelLoadState::Invalid};
    std::shared_ptr<const ModelData> data;
    std::string error;
};

class ModelResources {
public:
    ModelResources();
    ~ModelResources();
    ModelResources(const ModelResources&) = delete;
    ModelResources& operator=(const ModelResources&) = delete;

    [[nodiscard]] ModelHandle request(const std::filesystem::path& path, bool forceReload = false);
    [[nodiscard]] ModelLoadState state(ModelHandle handle) const;
    [[nodiscard]] std::vector<ModelCompletion> drainCompleted();

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace aeg::assets
