#pragma once
#include "authored_component.hpp"
#include <forge/runtime_resources.hpp>
#include <forge/runtime_ui.hpp>
#include <limits>
#include <memory>

namespace forge {
// Projects an already sampled clip document through the normal UI model owner.
// The copied Scene has no animation, simulation, physics or audio execution.
class AnimationUiPreview {
    struct Context {
        EngineContext engine;
        Scene scene;
        std::shared_ptr<UiRuntime> ui;
        Context(const std::filesystem::path& project, const Scene& source, Json types)
            : engine(WorldRole::Runtime, false,
                     {runtime_resources_module(project), ui_module(project),
                      detail::copied_authoring_module(std::move(types),
                                                      role_mask(WorldRole::Runtime))}),
              scene(engine.world()),
              ui(std::static_pointer_cast<UiRuntime>(engine.services().ui())) {
            scene.restore_snapshot(source.snapshot());
        }
    };
    std::unique_ptr<Context> context_;
    std::filesystem::path project_;
    EntityRef target_{};
    AssetId clip_{};
    Json types_;
    std::uint64_t source_revision_ = 0, document_generation_ = 0, generation_ = 0;

  public:
    void reset() { context_.reset(); }
    std::uint64_t generation() const { return generation_; }
    Json snapshot(const std::filesystem::path& project, const Scene& source, const Json& sampled,
                  EntityRef target, AssetId clip, std::uint64_t document_generation) {
        if (!clip || !document_generation || target.scene != source.asset_id() ||
            sampled.at("asset_id") != Json(source.asset_id()) ||
            !source.entity(target.entity.str()))
            throw std::runtime_error("Choose the current clip, project and preview target");
        const auto types = source.authored_types();
        if (!context_ || project_ != project || target_ != target || clip_ != clip ||
            source_revision_ != source.revision() || document_generation_ != document_generation ||
            types_ != types) {
            if (generation_ == std::numeric_limits<std::uint64_t>::max())
                throw std::runtime_error("Animation UI preview generation exhausted");
            auto candidate = std::make_unique<Context>(project, source, types);
            candidate->scene.reset(sampled);
            context_ = std::move(candidate);
            project_ = project;
            target_ = target;
            clip_ = clip;
            source_revision_ = source.revision();
            document_generation_ = document_generation;
            types_ = types;
            ++generation_;
        } else
            context_->scene.reset(sampled);
        const auto session =
            "clip/" + target.scene.str() + "/" + target.entity.str() + "/" + clip.str();
        return context_->ui->snapshot(context_->scene, session, generation_, 0, true);
    }
};
} // namespace forge
