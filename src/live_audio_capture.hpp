#pragma once
#include <forge/audio.hpp>
#include <forge/game_session.hpp>

namespace forge {
// The existing Play capture lease and wire operations, independent of rendering.
// AudioRuntime retains every mixer/callback/buffer; this owner holds copied visit
// witnesses and retargets its one lease at the trusted preparation boundary.
class LiveAudioCapture {
  public:
    explicit LiveAudioCapture(std::filesystem::path project);
    ~LiveAudioCapture();
    LiveAudioCapture(const LiveAudioCapture&) = delete;
    LiveAudioCapture& operator=(const LiveAudioCapture&) = delete;
    Json command(const Json&, std::uint64_t active_generation,
                 const std::shared_ptr<AudioRuntime>& active_audio, const Scene* active_scene,
                 bool settled);
    std::unique_ptr<GameScenePreparation> prepare(std::unique_ptr<GameScenePreparation>,
                                                  std::uint64_t ticket);

  private:
    struct Preparation;
    enum class Failure { None, Preparation, Handoff, VisitLimit, World };
    std::filesystem::path project_;
    std::shared_ptr<AudioRuntime> audio_;
    std::uint64_t initial_world_ = 0;
    AssetId initial_scene_;
    bool session_scope_ = false, following_ = false;
    Failure failure_ = Failure::None;
    Json visits_ = Json::array();
    Json visit(const Scene&, std::uint64_t, std::uint64_t) const;
    Json status(std::uint64_t active_generation) const;
    void adopt(Preparation&) noexcept;
};
} // namespace forge
