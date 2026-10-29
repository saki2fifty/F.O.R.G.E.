#pragma once
#include "editor/reflection_capture_task.hpp"
namespace forge::test {
// Only native delivery is replaced. Source encoding, folder checks, import
// review, real asset worker/publication, Scene history and saving are unchanged.
class RecordingReflectionCapture final : public ui::ReflectionCaptureDriver {
    unsigned size_, face_ = 0;
    std::function<bool()> hold_;
    bool busy_ = true;
    std::string reason_;

  public:
    explicit RecordingReflectionCapture(unsigned size, std::function<bool()> hold = {})
        : size_(size), hold_(std::move(hold)) {}
    void step(Diligent::IDeviceContext*) override {
        if (!reason_.empty())
            busy_ = false;
        else if (hold_ && hold_())
            return;
        else if (face_ < 6)
            busy_ = ++face_ != 6;
    }
    void cancel(const std::string& reason) override { reason_ = reason; }
    bool busy() const override { return busy_; }
    unsigned completed_faces() const override { return face_; }
    std::string diagnostic() const override { return reason_; }
    TextureData take() override {
        if (busy_ || !reason_.empty() || face_ != 6)
            throw std::runtime_error("Delivery taken before completion");
        TextureData value;
        value.dimension = TextureDimension::Cube;
        value.format = TextureFormat::RGBA16Float;
        value.semantic = TextureSemantic::HdrColor;
        value.alpha = TextureAlpha::Opaque;
        value.width = value.height = size_;
        value.subresources.resize(6);
        for (auto& face : value.subresources) {
            face.resize(std::size_t(size_) * size_ * 8);
            for (std::size_t i = 0; i < face.size(); i += 8) {
                constexpr std::array<unsigned char, 8> pixel{0, 0x40, 0, 0x44, 0, 0x46, 0, 0x3c};
                for (unsigned channel = 0; channel < 8; ++channel)
                    face[i + channel] = std::byte(pixel[channel]);
            }
        }
        return value;
    }
};
} // namespace forge::test
