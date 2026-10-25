#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <set>
#include <vector>

// Checks the exported scene image, not the editor chrome. A missing draw call,
// bad camera matrix, or failed model upload must not pass as a successful frame.
int main(int argc, char** argv) {
    try {
        if (argc < 2 || argc > 3 || (argc == 3 && std::string_view(argv[2]) != "--expect-blank"))
            throw std::runtime_error("usage: aeg_render_check capture.ppm [--expect-blank]");
        const bool expectBlank = argc == 3;
        std::ifstream input(std::filesystem::path(argv[1]), std::ios::binary);
        std::string magic;
        int width = 0, height = 0, maximum = 0;
        input >> magic >> width >> height >> maximum;
        if (!input || magic != "P6" || width < 64 || height < 64 || maximum != 255)
            throw std::runtime_error("invalid or missing PPM capture");
        input.get();
        std::vector<unsigned char> pixels(static_cast<std::size_t>(width) * height * 3);
        input.read(reinterpret_cast<char*>(pixels.data()), static_cast<std::streamsize>(pixels.size()));
        if (input.gcount() != static_cast<std::streamsize>(pixels.size()))
            throw std::runtime_error("truncated capture");
        const std::array<int, 3> background{pixels[0], pixels[1], pixels[2]};
        std::size_t foreground = 0;
        std::set<std::uint32_t> colors;
        for (std::size_t i = 0; i < pixels.size(); i += 3) {
            const int difference = std::abs(int(pixels[i]) - background[0]) +
                                   std::abs(int(pixels[i + 1]) - background[1]) +
                                   std::abs(int(pixels[i + 2]) - background[2]);
            if (difference > 45) ++foreground;
            colors.insert((std::uint32_t(pixels[i]) << 16) | (std::uint32_t(pixels[i + 1]) << 8) | pixels[i + 2]);
        }
        const auto count = static_cast<std::size_t>(width) * height;
        if (expectBlank && (foreground != 0 || colors.size() != 1))
            throw std::runtime_error("masked material should be entirely discarded");
        if (!expectBlank && (foreground < count / 100 || colors.size() < 8))
            throw std::runtime_error("scene is blank or model is not visibly rendered");
        std::cout << "PASS scene capture " << width << 'x' << height << ": " << foreground << " foreground pixels, "
                  << colors.size() << " colors\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "FAIL render check: " << error.what() << '\n';
        return 1;
    }
}
