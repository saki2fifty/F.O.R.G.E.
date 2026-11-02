#include "navigation_behavior_runtime.hpp"
#include "navigation_schema.hpp"
#include <chrono>
#include <forge/application_state_activation.hpp>
#include <set>
#include <thread>

namespace forge {
namespace {
void require(bool value, const char* why) {
    if (!value)
        throw std::runtime_error(why);
}
bool progress(const NavigationBehaviorCursor& value) {
    return value.state || value.last_transition || value.elapsed != 0 || value.transitions;
}
} // namespace
Json NavigationBehaviorPlayback::checkpoint() {
    check();
    for (const auto& [id, player] : players_) {
        (void)id;
        require(!player.frame, "Behavior checkpoint requires a completed navigation boundary");
    }
    const auto entities = controllers();
    std::set<AssetId> extra;
    for (auto id : entities) {
        const auto e = context_.world().entity(id);
        if (e.has<NavigationBehaviorCursor>() && progress(e.get<NavigationBehaviorCursor>())) {
            const auto asset = e.get<NavigationBehavior>().graph.id;
            require(bool(asset), "Behavior progress has no graph identity");
            extra.insert(asset);
        }
    }
    const std::vector<AssetId> required(extra.begin(), extra.end());
    if (!prepare(true, required))
        return Json(); // Optional host recovery cannot contain pending resource selections.
    Json rows = Json::array();
    const auto& descriptor = navigation_detail::navigation_descriptor("forge.navigation_behavior");
    for (auto id : entities) {
        const auto e = context_.world().entity(id);
        const auto& p = players_.at(id);
        const bool present = e.owns<NavigationBehaviorCursor>();
        const auto cursor =
            present ? e.get<NavigationBehaviorCursor>() : NavigationBehaviorCursor{};
        const bool pinned = p.config.enabled || progress(cursor);
        Json revision = nullptr, digest = nullptr;
        if (pinned) {
            const auto found = graphs_.find(p.config.graph.id);
            require(found != graphs_.end() && bool(found->second.lease),
                    "Behavior progress has no admitted graph revision");
            const auto& graph = found->second.lease;
            if (found->second.status.state != "ready" ||
                found->second.status.requested_revision != graph.identity().revision)
                return Json(); // Last-good live data may not be resolvable in a new world.
            validate_navigation_behavior_cursor(graph.get(), cursor);
            revision = graph.identity().revision;
            digest = graph->cooked_digest;
        } else
            require(cursor == NavigationBehaviorCursor{}, "Invalid inactive behavior progress");
        auto config = descriptor.read(e, true);
        detail::validate_reflected_value(e, e.get<NavigationBehavior>());
        detail::validate_components({{"forge.navigation_behavior", config}});
        rows.push_back({{"entity", *context_.reference(id)},
                        {"config", config},
                        {"owned", e.owns<NavigationBehavior>()},
                        {"progress_present", present},
                        {"cursor", present ? Json(cursor) : Json()},
                        {"revision", revision},
                        {"cooked_digest", digest},
                        {"issued", p.issued ? detail::encode_builtin_value(detail::Value{*p.issued})
                                            : Json()}});
    }
    require(rows.dump().size() <= 128 * 1024, "Behavior recovery exceeds 128 KiB");
    return rows;
}
void NavigationBehaviorPlayback::restore(const Json& rows) {
    check();
    ApplicationStateActivation conditions(context_);
    for (const auto& [id, player] : players_) {
        (void)id;
        require(!player.frame, "Behavior restore requires an unpublished completed boundary");
    }
    const auto entities = controllers();
    require(rows.is_array() && rows.size() == entities.size() && rows.size() <= 64 &&
                rows.dump().size() <= 128 * 1024,
            "Behavior recovery controller set differs");
    struct Pending {
        flecs::entity entity;
        NavigationBehavior config;
        NavigationBehaviorCursor cursor;
        bool owned = false, present = false, pinned = false;
        std::string revision, digest;
        std::optional<NavigationAgent> issued;
    };
    std::vector<Pending> pending;
    std::set<flecs::entity_t> expected(entities.begin(), entities.end()), seen;
    std::set<AssetId> required;
    const auto& descriptor = navigation_detail::navigation_descriptor("forge.navigation_behavior");
    const auto& agent = navigation_detail::navigation_descriptor("forge.navigation_agent");
    for (const auto& row : rows) {
        require(row.is_object() && row.size() == 8 && row.at("owned").is_boolean() &&
                    row.at("progress_present").is_boolean(),
                "Invalid behavior recovery fields");
        const auto e = resolve(row.at("entity").get<EntityRef>());
        require(e && expected.contains(e.id()) && seen.insert(e.id()).second,
                "Behavior recovery entity identity differs");
        detail::validate_components({{"forge.navigation_behavior", row.at("config")}});
        Pending p;
        p.entity = e;
        p.config = std::get<NavigationBehavior>(descriptor.decode(row.at("config")));
        p.owned = row.at("owned").get<bool>();
        p.present = row.at("progress_present").get<bool>();
        const bool enabled = p.config.enabled && conditions.active(e.id());
        require(!enabled || p.present, "Active behavior recovery needs cursor progress");
        require(enabled || row.at("issued").is_null(),
                "Disabled behavior recovery cannot own an issued destination");
        require(p.owned == e.owns<NavigationBehavior>() &&
                    (p.owned || p.config == e.get<NavigationBehavior>()),
                "Behavior recovery component ownership/inheritance differs");
        if (p.present)
            p.cursor = row.at("cursor").get<NavigationBehaviorCursor>();
        else
            require(row.at("cursor").is_null(), "Absent behavior cursor contains progress");
        p.pinned = enabled || progress(p.cursor);
        if (p.pinned) {
            require(bool(p.config.graph.id), "Behavior recovery progress requires a graph");
            p.revision = row.at("revision").get<std::string>();
            p.digest = row.at("cooked_digest").get<std::string>();
            required.insert(p.config.graph.id);
        } else
            require(row.at("revision").is_null() && row.at("cooked_digest").is_null() &&
                        p.cursor == NavigationBehaviorCursor{},
                    "Inactive behavior recovery contains selected progress");
        if (!row.at("issued").is_null()) {
            detail::validate_components({{"forge.navigation_agent", row.at("issued")}});
            p.issued = std::get<NavigationAgent>(agent.decode(row.at("issued")));
        }
        pending.push_back(std::move(p));
    }
    require(seen == expected, "Behavior recovery controller set differs");
    // Only an unpublished candidate may call this method. Full semantic and asset
    // validation follows before its known cursor progress becomes observable.
    for (const auto& p : pending)
        if (p.owned)
            p.entity.set<NavigationBehavior>(p.config);
    const std::vector<AssetId> extra(required.begin(), required.end());
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    for (;;) {
        if (context_.services().available(Capability::Resources))
            context_.services().resources()->synchronize();
        if (prepare(true, extra))
            break;
        require(std::chrono::steady_clock::now() < deadline, "Behavior recovery resource timeout");
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    for (const auto& p : pending) {
        if (!p.pinned)
            continue;
        const auto found = graphs_.find(p.config.graph.id);
        require(found != graphs_.end() && bool(found->second.lease),
                "Behavior recovery graph is unavailable");
        const auto& graph = found->second.lease;
        require(graph.identity().revision == p.revision && graph->cooked_digest == p.digest,
                "Behavior recovery graph revision differs; start clean Play");
        validate_navigation_behavior_cursor(graph.get(), p.cursor);
    }
    for (const auto& p : pending) {
        if (p.present)
            p.entity.set<NavigationBehaviorCursor>(p.cursor);
        else
            p.entity.remove<NavigationBehaviorCursor>();
        auto& player = players_.at(p.entity.id());
        player.config = p.config;
        player.config.enabled = player.config.enabled && conditions.active(p.entity.id());
        player.issued = p.issued;
        player.cooked_digest = p.digest;
        player.revision = p.revision;
        player.step_error.clear();
        player.preparation_error.clear();
        player.sampled = false;
    }
}
} // namespace forge
