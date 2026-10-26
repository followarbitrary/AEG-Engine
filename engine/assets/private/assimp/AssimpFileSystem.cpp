#include "AssimpFileSystem.hpp"
#include "ResourcePath.hpp"
#include <Utf8Path.hpp>
#include <assimp/IOStream.hpp>
#include <assimp/IOSystem.hpp>
#include <algorithm>
#include <cstdint>
#include <fstream>
#include <limits>
#include <memory>
#include <stdexcept>

namespace aeg::assets::detail {
namespace {
std::string genericUtf8(const std::filesystem::path& path) {
    const auto text = path.generic_u8string();
    return {reinterpret_cast<const char*>(text.data()), text.size()};
}
// Assimp otherwise uses narrow CRT file APIs on Windows. Opening through path
// keeps the full Unicode filename, including sidecar material/buffer files.
class FileStream final : public Assimp::IOStream {
public:
    explicit FileStream(const std::filesystem::path& path) : file_(path, std::ios::binary) {
        if (file_) {
            file_.seekg(0, std::ios::end);
            const auto end = file_.tellg();
            if (end < 0) {
                file_.close();
                return;
            }
            size_ = static_cast<std::size_t>(end);
            file_.seekg(0);
        }
    }
    bool good() const { return file_.is_open(); }
    std::size_t Read(void* buffer, std::size_t size, std::size_t count) override {
        if (size == 0 || count > std::numeric_limits<std::size_t>::max() / size) return 0;
        const auto bytes = std::min(size * count, size_ - std::min(position_, size_));
        file_.read(static_cast<char*>(buffer), static_cast<std::streamsize>(bytes));
        const auto read = static_cast<std::size_t>(file_.gcount());
        position_ += read;
        return read / size;
    }
    std::size_t Write(const void*, std::size_t, std::size_t) override { return 0; }
    aiReturn Seek(std::size_t offset, aiOrigin origin) override {
        // The Assimp API represents negative fseek offsets in size_t.
        const auto delta = static_cast<std::int64_t>(offset);
        std::size_t base = 0;
        if (origin == aiOrigin_CUR)
            base = position_;
        else if (origin == aiOrigin_END)
            base = size_;
        else if (origin != aiOrigin_SET)
            return aiReturn_FAILURE;
        if (delta < 0 && delta < -static_cast<std::int64_t>(base)) return aiReturn_FAILURE;
        if (delta > 0 && static_cast<std::uint64_t>(delta) > size_ - base) return aiReturn_FAILURE;
        const auto next = static_cast<std::size_t>(static_cast<std::int64_t>(base) + delta);
        file_.clear();
        file_.seekg(static_cast<std::streamoff>(next));
        if (!file_) return aiReturn_FAILURE;
        position_ = next;
        return aiReturn_SUCCESS;
    }
    std::size_t Tell() const override { return position_; }
    std::size_t FileSize() const override { return size_; }
    void Flush() override {}

private:
    std::ifstream file_;
    std::size_t size_{0}, position_{0};
};
class FileSystem final : public Assimp::IOSystem {
public:
    explicit FileSystem(const std::filesystem::path& source, std::vector<std::filesystem::path>* openedFiles)
        : source_(source), directory_(source.parent_path()), gltf_(isGltf(source)), openedFiles_(openedFiles) {
        directoryPrefix_ = genericUtf8(directory_);
        if (!directoryPrefix_.ends_with('/')) directoryPrefix_ += '/';
    }
    bool Exists(const char* path) const override {
        try {
            std::error_code ec;
            return std::filesystem::is_regular_file(resolve(path), ec);
        } catch (const std::runtime_error&) {
            return false;
        }
    }
    char getOsSeparator() const override { return '/'; }
    Assimp::IOStream* Open(const char* path, const char* mode = "rb") override {
        if (!mode || mode[0] != 'r') return nullptr;
        try {
            const auto resolved = resolve(path);
            auto file = std::make_unique<FileStream>(resolved);
            if (!file->good()) return nullptr;
            if (openedFiles_ && resolved.lexically_normal() != source_ &&
                std::find(openedFiles_->begin(), openedFiles_->end(), resolved) == openedFiles_->end())
                openedFiles_->push_back(resolved);
            return file.release();
        } catch (const std::runtime_error&) {
            return nullptr;
        }
    }
    void Close(Assimp::IOStream* stream) override { delete stream; }

private:
    std::filesystem::path resolve(std::string_view name) const {
        const auto raw = foundation::pathFromUtf8(name);
        if (!gltf_ || raw.lexically_normal() == source_) return raw;
        // Assimp prepends the model directory to external buffer URIs. Decode
        // only the reference suffix: the model path is a literal filesystem
        // path and can itself contain names such as "literal%20directory".
        const auto request = genericUtf8(raw);
        if (request.starts_with(directoryPrefix_)) {
            auto reference = std::string_view(request).substr(directoryPrefix_.size());
            // Assimp can append '/' to a directory already ending in '\\'.
            // Remove the duplicate separator so path joining stays relative.
            while (reference.starts_with('/')) reference.remove_prefix(1);
            return directory_ / foundation::pathFromUtf8(percentDecode(reference));
        }
        if (raw.is_relative()) return directory_ / foundation::pathFromUtf8(percentDecode(request));
        return raw;
    }
    std::filesystem::path source_, directory_;
    std::string directoryPrefix_;
    bool gltf_;
    std::vector<std::filesystem::path>* openedFiles_;
};
}
Assimp::IOSystem* createFileSystem(const std::filesystem::path& source,
                                   std::vector<std::filesystem::path>* openedFiles) {
    return new FileSystem(source, openedFiles);
}
}
