#include <RhiDevice.hpp>
#include <stdexcept>

namespace aeg::rhi {
namespace {
class ImmediateUploadBatch final : public UploadBatch {
public:
    explicit ImmediateUploadBatch(Device& device) : device_(device) {}

    std::unique_ptr<Buffer> uploadBuffer(std::span<const std::byte> bytes, BufferUsage usage) override {
        ensureActive();
        return device_.uploadBuffer(bytes, usage);
    }

    std::unique_ptr<Texture> uploadTexture(const TextureDesc& desc,
                                           std::span<const std::byte> pixels) override {
        ensureActive();
        return device_.uploadTexture(desc, pixels);
    }

    void end() override {
        ensureActive();
        terminated_ = true;
    }

    void cancel() noexcept override { terminated_ = true; }

private:
    void ensureActive() const {
        if (terminated_) throw std::logic_error("Upload batch is no longer active");
    }

    Device& device_;
    bool terminated_{};
};
} // namespace

std::unique_ptr<UploadBatch> Device::beginUploadBatch() {
    return std::make_unique<ImmediateUploadBatch>(*this);
}
} // namespace aeg::rhi
