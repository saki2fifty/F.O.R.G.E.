#pragma once
#include "grid.hpp"
#include <optional>
namespace forge {
// Editor-owned authoring reads only. Runtime Flecs systems can change values without
// an authoring revision, so runtime snapshots must never use this cache.
class AuthoringSnapshot {
  public:
    const Json& document(const Scene& scene) {
        if (owner_ != &scene || revision_ != scene.revision()) {
            auto next = scene.document();
            document_ = std::move(next);
            effective_.reset();
            owner_ = &scene;
            revision_ = scene.revision();
        }
        return document_;
    }
    const Json& effective(const Scene& scene) {
        document(scene);
        if (!effective_)
            effective_ = render_document(document_);
        return *effective_;
    }

  private:
    const Scene* owner_ = nullptr;
    std::uint64_t revision_ = 0;
    Json document_;
    std::optional<Json> effective_;
};
// Rebuild on source changes, every live preview, and once when a preview ends.
// Build callbacks are lazy: unchanged frames do no JSON copies or prefab resolution.
class PreviewSnapshot {
  public:
    template <class Build>
    const Json& get(std::uint64_t revision, bool playing, bool transient, Build build) {
        if (!ready_ || revision != revision_ || playing != playing_ || transient || transient_) {
            auto next = build();
            document_ = std::move(next);
            revision_ = revision;
            playing_ = playing;
            transient_ = transient;
            ready_ = true;
            ++generation_;
        }
        return document_;
    }
    std::uint64_t generation() const { return generation_; }

  private:
    Json document_;
    std::uint64_t revision_ = 0, generation_ = 0;
    bool ready_ = false, playing_ = false, transient_ = false;
};
struct ViewportFrameKey {
    std::uint64_t generation;
    unsigned width, height;
    EditorCamera::Vec target;
    float yaw, pitch, distance;
    GridSettings grid;
    bool operator==(const ViewportFrameKey&) const = default;
};
inline ViewportFrameKey viewport_frame_key(std::uint64_t generation, unsigned width,
                                           unsigned height, const EditorCamera& camera,
                                           GridSettings grid = {}) {
    return {generation, width,        height,          camera.target,
            camera.yaw, camera.pitch, camera.distance, grid};
}
} // namespace forge
