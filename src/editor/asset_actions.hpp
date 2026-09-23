#pragma once
#include "../asset_file_service.hpp"
#include "actions.hpp"
namespace forge::ui {
// One context snapshot for menu/button/palette routes. Targets are copied stable
// identities; application services still revalidate revisions and project ownership.
struct AssetActionContext {
    std::optional<AssetRecord> target;
    std::vector<AssetId> selected;
    std::string blocked, file_blocked;
    bool openable = false, reimportable = false, placeable = false;
};
struct AssetActionHandlers {
    std::function<void()> import_files, cache;
    std::function<void(const AssetRecord&)> open, place, collision;
    std::function<void(const std::vector<AssetId>&)> reimport;
    std::function<void(const AssetRecord&, AssetFileAction)> files;
};
inline EditorActions asset_actions(AssetActionContext context,
                                   const AssetActionHandlers& handlers) {
    EditorActions actions;
    const auto target = context.target;
    auto add = [&](std::string id, std::string label, bool supported, std::string reason,
                   std::string help, std::function<void()> run) {
        if (!context.blocked.empty())
            reason = context.blocked;
        actions.entries.push_back({std::move(id),
                                   std::move(label),
                                   {},
                                   std::move(help),
                                   context.blocked.empty() && supported && reason.empty(),
                                   std::move(run),
                                   std::move(reason)});
    };
    add("asset.import", "Assets / Import files...", bool(handlers.import_files), {},
        "Choose source files and review the destination. Source publication is separate from scene "
        "Undo.",
        handlers.import_files);
    add("asset.open", "Assets / Open selected", target && context.openable,
        target && context.openable ? "" : "Select one asset with a supported editor.",
        "Open the selected asset in its registered document. Save and Undo belong to that "
        "document.",
        [target, fn = handlers.open] {
            if (target && fn)
                fn(*target);
        });
    add("asset.reimport", "Assets / Reimport selected", context.reimportable,
        context.reimportable ? "" : "Select registered assets with a supported importer.",
        "Queue the selected source owners for validation. Failed imports keep the last usable "
        "revision. Scene Undo does not undo publication.",
        [ids = context.selected, fn = handlers.reimport] {
            if (fn)
                fn(ids);
        });
    add("asset.place", "Assets / Place selected in Scene", target && context.placeable,
        target && context.placeable ? "" : "Select a Model, Mesh or Prefab asset.",
        "Place the selected asset at the creation target. One scene Undo step; source contents "
        "stay unchanged.",
        [target, fn = handlers.place] {
            if (target && fn)
                fn(*target);
        });
    add("asset.collision", "Assets / Create Collision from Mesh...",
        target && target->type == "mesh" && bool(handlers.collision),
        target && target->type == "mesh"
            ? ""
            : "Select a Mesh member in Content. Expand its Model to choose geometry.",
        "Create a separate convex or static triangle collision asset. The rendered Mesh is "
        "unchanged.",
        [target, fn = handlers.collision] {
            if (target && fn)
                fn(*target);
        });
    for (auto [id, label, operation] :
         {std::tuple{"asset.move", "Assets / Rename / Move source...", AssetFileAction::Move},
          std::tuple{"asset.duplicate", "Assets / Duplicate source...", AssetFileAction::Duplicate},
          std::tuple{"asset.delete", "Assets / Delete source...", AssetFileAction::Delete}}) {
        const bool owner = target && !target->subasset;
        add(id, label, owner,
            owner ? context.file_blocked
                  : "Select a source asset; generated members belong to their source owner.",
            "Review file changes and reference impact before committing. Source operations are "
            "separate from scene Undo.",
            [target, operation, fn = handlers.files] {
                if (target && fn)
                    fn(*target, operation);
            });
    }
    add("asset.cache", "Assets / Derived cache...", bool(handlers.cache), {},
        "Inspect, verify or clear disposable imported data after import jobs drain. Authored files "
        "and scene history are preserved.",
        handlers.cache);
    return actions;
}
} // namespace forge::ui
