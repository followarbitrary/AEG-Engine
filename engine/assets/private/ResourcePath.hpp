#pragma once
#include <algorithm>
#include <cctype>
#include <filesystem>
#include <stdexcept>
#include <string>
#include <string_view>

namespace aeg::assets::detail {
inline std::string lowerExtension(const std::filesystem::path& path) {
    auto extension = path.extension().string();
    std::transform(extension.begin(), extension.end(), extension.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return extension;
}
inline bool isGltf(const std::filesystem::path& path) {
    const auto extension = lowerExtension(path);
    return extension == ".gltf" || extension == ".glb";
}
inline std::string percentDecode(std::string_view uri) {
    const auto hex = [](char c) -> int {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'a' && c <= 'f') return c - 'a' + 10;
        if (c >= 'A' && c <= 'F') return c - 'A' + 10;
        return -1;
    };
    std::string result;
    result.reserve(uri.size());
    for (std::size_t i = 0; i < uri.size(); ++i) {
        if (uri[i] != '%') {
            if (uri[i] == '\0') throw std::runtime_error("NUL is not valid in a resource URI");
            result += uri[i];
            continue;
        }
        if (i + 2 >= uri.size() || hex(uri[i + 1]) < 0 || hex(uri[i + 2]) < 0)
            throw std::runtime_error("Invalid percent escape in resource URI: " + std::string(uri));
        const auto value = static_cast<char>(hex(uri[i + 1]) * 16 + hex(uri[i + 2]));
        if (value == '\0') throw std::runtime_error("NUL is not valid in a resource URI");
        result += value;
        i += 2;
    }
    return result;
}
}
