#include <aeg/Editor.hpp>

#include <charconv>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <shellapi.h>
#endif

namespace {
#ifdef _WIN32
std::string utf8(const std::filesystem::path& path) {
    const auto value = path.u8string();
    return {reinterpret_cast<const char*>(value.data()), value.size()};
}
#endif

std::filesystem::path pathFromText(std::string_view value) {
    return std::filesystem::path(std::u8string(value.begin(), value.end()));
}

std::filesystem::path executableDirectory([[maybe_unused]] const char* argv0) {
#ifdef _WIN32
    std::wstring buffer(512, L'\0');
    for (;;) {
        const auto length = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
        if (length == 0) throw std::runtime_error("Cannot locate the executable directory.");
        if (length < buffer.size()) {
            buffer.resize(length);
            return std::filesystem::path(buffer).parent_path();
        }
        buffer.resize(buffer.size() * 2);
    }
#else
    std::error_code error;
    auto path = std::filesystem::read_symlink("/proc/self/exe", error);
    if (!error) return path.parent_path();
    return std::filesystem::absolute(argv0).parent_path();
#endif
}

std::vector<std::string> arguments([[maybe_unused]] int argc, [[maybe_unused]] char** argv) {
#ifdef _WIN32
    int wideCount = 0;
    auto wideArgs = CommandLineToArgvW(GetCommandLineW(), &wideCount);
    if (!wideArgs) throw std::runtime_error("Cannot decode command line.");
    struct Release {
        LPWSTR* value;
        ~Release() { LocalFree(value); }
    } release{wideArgs};
    std::vector<std::string> result;
    for (int i = 1; i < wideCount; ++i) result.push_back(utf8(std::filesystem::path(wideArgs[i])));
    return result;
#else
    return {argv + 1, argv + argc};
#endif
}

void printHelp() {
    std::cout << "AEG Engine - Vulkan model viewer\n"
                 "Usage: aeg_editor [--model PATH] [--frames N] [--capture PATH] [--no-validation] [--hidden]\n"
                 "  --model PATH       Open an OBJ, glTF, GLB or PMX model\n"
                 "  --frames N         Exit after N rendered frames (positive integer)\n"
                 "  --capture PATH     Export the last viewport to a PPM image\n"
                 "  --no-validation    Disable Vulkan validation layers\n"
                 "  --hidden           Render without showing or focusing the window\n"
                 "  --help             Show this help\n"
                 "Controls: drag to orbit, middle-drag to pan, wheel to zoom, F to frame.\n";
}
}

int main(int argc, char** argv) {
    try {
        aeg::EditorOptions options;
        const auto args = arguments(argc, argv);
        for (std::size_t i = 0; i < args.size(); ++i) {
            const auto& arg = args[i];
            if (arg == "--help" || arg == "-h") {
                printHelp();
                return 0;
            }
            if (arg == "--no-validation") {
                options.validation = false;
                continue;
            }
            if (arg == "--hidden") {
                options.hidden = true;
                continue;
            }
            if (arg != "--model" && arg != "--frames" && arg != "--capture")
                throw std::runtime_error("Unknown argument: " + arg + ". Use --help.");
            if (i + 1 == args.size() || args[i + 1].starts_with("--"))
                throw std::runtime_error("Missing value for " + arg + '.');
            const auto& value = args[++i];
            if (value.empty()) throw std::runtime_error("Empty value for " + arg + '.');
            if (arg == "--frames") {
                std::uint32_t count = 0;
                const auto [end, error] = std::from_chars(value.data(), value.data() + value.size(), count);
                if (error != std::errc{} || end != value.data() + value.size() || count == 0)
                    throw std::runtime_error("--frames requires a positive 32-bit integer.");
                options.frameLimit = count;
            } else if (arg == "--model")
                options.initialModel = pathFromText(value);
            else
                options.capturePath = pathFromText(value);
        }
        options.resourceDirectory = executableDirectory(argv[0]);
        return aeg::runEditor(options);
    } catch (const std::exception& error) {
        std::cerr << "AEG error: " << error.what() << '\n';
        return 1;
    }
}
