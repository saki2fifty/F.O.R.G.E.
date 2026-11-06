#pragma once
#include "paint_texture_source.hpp"
#include <functional>
#include <future>
#include <optional>
#include <vector>

namespace forge {
// Detached CPU preparation only. The caller must compare session/revision before
// adopting through AssetSourceDocument; this task never owns history or Save.
class PaintTextureTask {
  public:
    struct Result {
        std::uint64_t session{}, revision{};
        PaintTextureValue value;
        std::string error;
        std::string label;
    };
    ~PaintTextureTask() { cancel(); }
    bool pending() const { return future_.valid(); }
    void start(PaintTextureValue, std::vector<PaintPoint>, PaintBrush, std::uint64_t session,
               std::uint64_t revision);
    void mapped_stroke(PaintTextureValue, std::function<std::vector<PaintPoint>(std::stop_token)>,
                       PaintBrush, std::uint64_t session, std::uint64_t revision);
    void copy_pixels(PaintTextureValue, std::function<Rgba8Pixels(std::stop_token)>,
                     std::uint64_t session, std::uint64_t revision);
    std::optional<Result> poll();
    void cancel();

  private:
    std::stop_source stop_;
    std::future<Result> future_;
    void launch(std::function<PaintTextureValue(std::stop_token)>, std::string label,
                std::uint64_t session, std::uint64_t revision);
};
} // namespace forge
