#include "animation_preview.hpp"
#include <cmath>
namespace forge {
SkeletalAnimationPreview::PreviewWorld::PreviewWorld(const std::filesystem::path& project,
                                                     const Json& source, EntityRef ref)
    : engine(WorldRole::Runtime, false, {animation_module(project)}), scene(engine.world()),
      animation(animation_runtime(engine.world())), target(ref) {
    scene.restore_snapshot(source);
    (void)actor();
}
flecs::entity SkeletalAnimationPreview::PreviewWorld::actor() {
    const auto resolved = engine.world().resolve(target);
    if (resolved.state != WorldContext::ResolveState::Available)
        throw std::runtime_error("Animation preview target is unavailable");
    const auto entity = engine.world().world().entity(resolved.entity);
    if (!entity.has<Animator>())
        throw std::runtime_error("Choose an object with an Animator to preview");
    return entity;
}
void SkeletalAnimationPreview::start(const std::filesystem::path& project, Json source,
                                     EntityRef target) {
    bool found = false;
    for (auto& item : source.at("entities")) {
        auto& components = item.at("components");
        components.erase("forge.transform_animator");
        if (item.at("id").get<EntityId>() == target.entity &&
            components.contains("forge.animator")) {
            components["forge.animator"]["play_on_start"] = true;
            components["forge.animator"]["enabled"] = true;
            found = true;
        } else
            components.erase("forge.animator");
    }
    if (!found)
        throw std::runtime_error("Choose an object with an Animator to preview");
    auto candidate = std::make_unique<PreviewWorld>(project, source, target);
    world_ = std::move(candidate);
}
bool SkeletalAnimationPreview::prepare() {
    if (!world_)
        return false;
    if (!world_->ready)
        world_->ready = world_->animation->prepare_initial_pose();
    return world_->ready;
}
void SkeletalAnimationPreview::configure(float weight, float seconds, bool loop, float speed) {
    if (!prepare())
        return;
    auto actor = world_->actor();
    auto config = actor.get<Animator>();
    if (!std::isfinite(weight) || weight < 0 || weight > 1 || !std::isfinite(seconds) ||
        seconds < 0 || seconds > 10 || !std::isfinite(speed) || speed < 0 || speed > 4 ||
        (!config.blend_clip.id && weight != 0))
        throw std::runtime_error("Invalid animation preview settings");
    config.blend_weight = weight;
    config.transition_seconds = seconds;
    config.loop = loop;
    config.playback_speed = speed;
    if (config == actor.get<Animator>())
        return;
    actor.set(config);
    world_->animation->synchronize();
    (void)world_->animation->prepare_initial_pose();
}
void SkeletalAnimationPreview::seek(double time) {
    if (!prepare())
        return;
    auto state = world_->animation->checkpoint();
    auto& entry = state.at("entries").at(0);
    const double duration = pose().at("duration");
    if (!std::isfinite(time) || time < 0 || time > duration)
        throw std::runtime_error("Animation preview time exceeds clip duration");
    entry["time"] = time;
    entry["playing"] = true;
    world_->animation->restore(state);
    (void)world_->animation->prepare_initial_pose();
}
void SkeletalAnimationPreview::tick(float seconds) {
    if (prepare())
        world_->animation->tick(seconds);
}
Json SkeletalAnimationPreview::pose() {
    if (!prepare())
        return nullptr;
    return world_->animation->presentation(world_->actor().id(), 1);
}
Json SkeletalAnimationPreview::snapshot() {
    if (!prepare())
        return nullptr;
    auto result = world_->scene.effective_document();
    auto sampled = pose();
    for (auto& entity : result.at("entities"))
        if (entity.at("id").get<EntityId>() == world_->target.entity) {
            entity["animation_pose"] = std::move(sampled);
            entity["model_animation_ready"] =
                world_->animation->model_pose_ready(world_->actor().id());
        }
    return result;
}
} // namespace forge
