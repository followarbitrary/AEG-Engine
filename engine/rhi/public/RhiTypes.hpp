#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <limits>
#include <type_traits>
#include <utility>

namespace aeg::rhi {
struct Extent2D {
    std::uint32_t width{}, height{};
    bool operator==(const Extent2D&) const = default;
};
enum class WindowSystem { Win32 };
struct NativeWindow {
    WindowSystem system{WindowSystem::Win32};
    void* window{};
    void* application{};
    std::function<Extent2D()> framebufferExtent;
};
struct DeviceDesc {
    NativeWindow window;
    bool validation{true};
};
enum class Format {
    Undefined,
    Rgba8Unorm,
    Rgba8Srgb,
    Bgra8Unorm,
    Bgra8Srgb,
    Rgba16Float,
    Rgba32Float,
    R32Float,
    D32Float,
    D24UnormS8,
    D16Unorm
};
enum class TextureUsage : std::uint32_t {
    None = 0,
    Sampled = 1,
    ColorAttachment = 2,
    DepthAttachment = 4,
    CopySource = 8,
    CopyDestination = 16
};
enum class BufferUsage : std::uint32_t {
    None = 0,
    Vertex = 1,
    Index = 2,
    Uniform = 4,
    CopySource = 8,
    CopyDestination = 16
};
enum class ShaderStage : std::uint32_t { None = 0, Vertex = 1, Fragment = 2 };
template <class T>
concept Flags = std::is_same_v<T, TextureUsage> || std::is_same_v<T, BufferUsage> || std::is_same_v<T, ShaderStage>;
template <Flags T> constexpr T operator|(T a, T b) {
    return static_cast<T>(std::to_underlying(a) | std::to_underlying(b));
}
template <Flags T> constexpr bool contains(T flags, T value) {
    return (std::to_underlying(flags) & std::to_underlying(value)) == std::to_underlying(value);
}
enum class MemoryUsage { Device, Upload, Readback };
enum class ResourceState { Undefined, Sampled, ColorAttachment, DepthAttachment, CopySource, CopyDestination, Present };
enum class LoadOp { Clear, Load, Discard };
enum class StoreOp { Store, Discard };
enum class Filter { Nearest, Linear };
enum class AddressMode { Repeat, ClampToEdge };
enum class VertexFormat { Float2, Float3, Float4 };
inline constexpr std::uint32_t unusedAttachment = std::numeric_limits<std::uint32_t>::max();
struct TextureSupport {
    bool supported{};
    std::uint32_t sampleMask{};
    Extent2D maximumExtent{};
    bool linearFiltering{};
};
struct DeviceCapabilities {
    std::uint32_t maximumImageDimension{1}, maximumFramebufferDimension{1}, maximumColorAttachments{1};
    Format depthFormat{Format::Undefined};
};
struct ClearValue {
    std::array<float, 4> color{};
    float depth{1};
    std::uint32_t stencil{};
};
[[nodiscard]] constexpr bool isDepthFormat(Format format) {
    return format == Format::D32Float || format == Format::D24UnormS8 || format == Format::D16Unorm;
}
[[nodiscard]] std::uint32_t formatByteSize(Format format);
}
