#include "model_placement.hpp"
#include "model_view_components.hpp"
#include "render_values.hpp"
#include "scene_draft.hpp"
#include "spatial_document.hpp"
#include <algorithm>
#include <forge/authoring.hpp>
#include <forge/model_asset.hpp>
#include <forge/primitive_catalog.hpp>
#include <set>
namespace forge::asset_detail {
namespace {
void require(bool ok, const char* message) {
    if (!ok)
        throw std::runtime_error(message);
}
Json components(const LocalTransform& local) {
    // Same independent authored channels and numerical admission as every scene
    // command. In particular, do not decompose explicit singular source TRS.
    const auto scale = checked_local_scale({local.scale.x, local.scale.y, local.scale.z});
    const auto rotation = normalized(local.rotation);
    return {{"forge.primitive", {{"kind", no_primitive}}},
            {"forge.local_translation",
             {{"x", local.translation.x}, {"y", local.translation.y}, {"z", local.translation.z}}},
            {"forge.local_rotation",
             {{"x", rotation.x}, {"y", rotation.y}, {"z", rotation.z}, {"w", rotation.w}}},
            {"forge.local_scale", {{"x", scale.x}, {"y", scale.y}, {"z", scale.z}}}};
}
LocalTransform local_transform(const Json& node) {
    const auto& value = node.at("trs");
    const auto& t = value.at("translation");
    const auto& r = value.at("rotation");
    const auto& s = value.at("scale");
    LocalTransform local;
    local.translation = {t.at(0), t.at(1), t.at(2)};
    local.rotation = {r.at(0), r.at(1), r.at(2), r.at(3)};
    local.scale = checked_local_scale({s.at(0), s.at(1), s.at(2)});
    return local;
}
} // namespace
ModelPlacementCandidate prepare_model_placement(const ModelSelection& selected, AssetId scene,
                                                std::uint64_t revision,
                                                const ModelPlacementOptions& options) {
    require(bool(scene) && bool(selected.owner), "Model placement needs scene/model identities");
    require(selected.index.version >= 3,
            "Reimport this model to obtain durable node identities before placement");
    require(!options.name.empty() && options.name.size() <= 4096 &&
                options.name.find('\0') == std::string::npos,
            "Model placement needs a bounded display name");
    const auto& hierarchy = selected.index.hierarchy;
    const auto& nodes = hierarchy.at("nodes");
    const auto& scenes = hierarchy.at("scenes");
    Json roots;
    if (options.source_scene) {
        require(*options.source_scene < scenes.size(), "Model scene selection is out of range");
        roots = scenes.at(*options.source_scene);
    } else if (!hierarchy.at("default_scene").is_null()) {
        roots = scenes.at(hierarchy.at("default_scene").get<std::size_t>());
    } else if (scenes.size() == 1) {
        roots = scenes.at(0);
    } else {
        require(scenes.empty(), "Choose which model scene to place; no default is declared");
        roots = Json::array();
        for (std::size_t i = 0; i < nodes.size(); ++i)
            if (nodes[i].at("parent").is_null())
                roots.push_back(i);
    }
    std::vector<std::vector<std::size_t>> children(nodes.size());
    for (std::size_t i = 0; i < nodes.size(); ++i)
        if (!nodes[i].at("parent").is_null())
            children.at(nodes[i].at("parent").get<std::size_t>()).push_back(i);
    std::vector<std::size_t> order;
    std::set<std::size_t> included;
    for (const auto& root : roots)
        order.push_back(root.get<std::size_t>());
    for (std::size_t cursor = 0; cursor < order.size(); ++cursor) {
        const auto node = order[cursor];
        require(node < nodes.size() && included.insert(node).second,
                "Model placement contains repeated or invalid nodes");
        require(order.size() <= 9999, "Model placement exceeds the scene entity profile");
        order.insert(order.end(), children[node].begin(), children[node].end());
    }
    for (const auto index : order) {
        if (hierarchy.contains("animation")) {
            const auto& plan = hierarchy.at("animation").at("plan");
            const auto& skin = plan.at("node_skins").at(index);
            if (!skin.is_null())
                for (const auto& joint : plan.at("skins").at(skin.get<std::size_t>()).at("joints"))
                    require(
                        included.contains(
                            plan.at("joint_nodes").at(joint.get<std::size_t>()).get<std::size_t>()),
                        "Selected model scene omits a required skin joint");
        }
    }
    ModelPlacementCandidate result;
    result.scene = scene;
    result.scene_revision = revision;
    result.model = selected.owner;
    result.model_generation = selected.generation;
    result.model_revision = selected.revision;
    result.root = EntityId::generate();
    auto root_components = components(options.transform);
    root_components["forge.model_source"] = {{"model", selected.owner}, {"node", nullptr}};
    if (options.clip.id) {
        require(hierarchy.contains("animation"), "Selected model has no animation family");
        const auto& clip = selected.member(options.clip.id);
        require(clip.identity.type == AnimationClipAsset::type,
                "Selected model animation is not a clip");
        const auto& animation = hierarchy.at("animation");
        const auto skeleton_address = animation.at("skeleton").get<std::string>();
        require(clip.bindings.at("skeleton") == skeleton_address &&
                    std::find(animation.at("clips").begin(), animation.at("clips").end(),
                              clip.identity.address) != animation.at("clips").end(),
                "Selected clip does not belong to the model animation family");
        const auto skeleton = selected.bindings.at(skeleton_address);
        require(selected.member(skeleton).identity.type == SkeletonAsset::type,
                "Selected model skeleton has the wrong asset type");
        root_components["forge.animator"] = {{"skeleton", skeleton}, {"clip", options.clip.id},
                                             {"enabled", true},      {"play_on_start", true},
                                             {"loop", true},         {"playback_speed", 1}};
    }
    result.entities = Json::array({{{"id", result.root},
                                    {"name", options.name},
                                    {"spatial", {{"mode", "follow_structure"}}},
                                    {"components", std::move(root_components)}}});
    std::map<std::size_t, AssetId> node_assets;
    for (const auto& member : selected.index.members)
        if (member.node)
            require(node_assets.emplace(*member.node, selected.bindings.at(member.identity.address))
                        .second,
                    "Model node has duplicate logical identity");
    std::map<std::size_t, EntityId> ids;
    std::set<EntityId> allocated{result.root};
    for (const auto index : order) {
        EntityId id;
        do {
            id = EntityId::generate();
        } while (!allocated.insert(id).second);
        ids.emplace(index, id);
    }
    for (const auto index : order) {
        const auto& source = nodes[index];
        auto values = components(local_transform(source));
        if (!source.value("visible", true))
            values["forge.node_visibility"] = {{"visible", false}};
        if (!source.value("selectable", true))
            values["forge.node_selectability"] = {{"selectable", false}};
        values["forge.model_source"] = {{"model", selected.owner}, {"node", node_assets.at(index)}};
        if (!source.at("mesh").is_null()) {
            const auto mesh = selected.bindings.at(source.at("mesh").get<std::string>());
            require(selected.member(mesh).identity.type == MeshAsset::type,
                    "Model node mesh has the wrong asset type");
            values["forge.mesh_renderer"] = {{"mesh", mesh},         {"materials", Json::array()},
                                             {"enabled", true},      {"visible", true},
                                             {"cast_shadows", true}, {"receive_shadows", true},
                                             {"layers", UINT32_MAX}};
        }
        if (!source.at("camera").is_null())
            values["forge.camera"] = detail::render_value(model_camera_component(
                hierarchy.at("cameras").at(source.at("camera").get<std::size_t>())));
        if (!source.at("light").is_null())
            values["forge.light"] = detail::render_value(model_light_component(
                hierarchy.at("lights").at(source.at("light").get<std::size_t>())));
        auto parent = result.root;
        if (!source.at("parent").is_null()) {
            const auto found = ids.find(source.at("parent").get<std::size_t>());
            require(found != ids.end(), "Model scene root has an unplaced structural parent");
            parent = found->second;
        }
        auto name = source.at("name").get<std::string>();
        if (name.empty())
            name = "Node";
        result.entities.push_back({{"id", ids.at(index)},
                                   {"name", name},
                                   {"parent", parent},
                                   {"spatial", {{"mode", "follow_structure"}}},
                                   {"components", std::move(values)}});
    }
    Json candidate{{"version", 3}, {"asset_id", scene}, {"entities", result.entities}};
    require(candidate.dump().size() <= 8 * 1024 * 1024,
            "Model placement exceeds the scene byte profile");
    detail::promote_scale_format(candidate, "entities", 5);
    Scene::validate_document(candidate);
    return result;
}
EntityId instantiate_model(Scene& scene, const AssetCatalog& catalog,
                           const ModelPlacementCandidate& candidate) {
    require(scene.asset_id() == candidate.scene && scene.revision() == candidate.scene_revision,
            "Model placement target changed; prepare placement again");
    const auto found = catalog.records().find(candidate.model);
    require(found != catalog.records().end() && found->second.type == ModelAsset::type &&
                !found->second.subasset,
            "Model placement source is unavailable");
    const auto& selected = found->second.metadata.at("forge.import");
    require(selected.at("key") == candidate.model_revision &&
                selected.at("generation") == candidate.model_generation,
            "Model revision changed; prepare placement again");
    auto member = [&](AssetId id, std::string_view type) -> const AssetRecord& {
        const auto value = catalog.records().find(id);
        require(
            value != catalog.records().end() && value->second.type == type &&
                value->second.subasset && !value->second.subasset->removed &&
                value->second.subasset->owner == candidate.model &&
                value->second.metadata.at("forge.import") == selected,
            "Model placement member is missing, removed, incompatible or from another revision");
        return value->second;
    };
    require(candidate.entities.is_array() && !candidate.entities.empty() &&
                candidate.entities.size() <= 10000,
            "Invalid model placement entities");
    for (const auto& entity : candidate.entities) {
        const auto& values = entity.at("components");
        const auto& source = values.at("forge.model_source");
        require(source.at("model").get<AssetId>() == candidate.model,
                "Placed node belongs to a different model");
        if (entity.at("id").get<EntityId>() == candidate.root) {
            require(source.at("node").is_null() && !values.contains("forge.mesh_renderer"),
                    "Model placement wrapper must not impersonate a source node");
            if (values.contains("forge.animator")) {
                const auto& animator = values.at("forge.animator");
                const auto skeleton = animator.at("skeleton").get<AssetId>();
                (void)member(skeleton, SkeletonAsset::type);
                const auto& clip =
                    member(animator.at("clip").get<AssetId>(), AnimationClipAsset::type);
                const auto dependency =
                    std::find_if(clip.dependency_edges.begin(), clip.dependency_edges.end(),
                                 [](const auto& edge) { return edge.role == "skeleton"; });
                require(dependency != clip.dependency_edges.end() && dependency->target == skeleton,
                        "Placed clip and skeleton belong to different animation families");
            }
            continue;
        }
        const auto& node = member(source.at("node").get<AssetId>(), ModelNodeAsset::type);
        const auto mesh = std::find_if(node.dependency_edges.begin(), node.dependency_edges.end(),
                                       [](const auto& edge) { return edge.role == "mesh"; });
        require((mesh != node.dependency_edges.end()) == values.contains("forge.mesh_renderer"),
                "Placed node mesh presence differs from source provenance");
        if (mesh != node.dependency_edges.end()) {
            const auto id = values.at("forge.mesh_renderer").at("mesh").get<AssetId>();
            require(mesh->target == id, "Placed node mesh differs from source binding");
            (void)member(id, MeshAsset::type);
        }
    }
    auto document = scene.document();
    require(candidate.entities.is_array() && !candidate.entities.empty() &&
                candidate.entities.front().at("id").get<EntityId>() == candidate.root &&
                document.at("entities").size() + candidate.entities.size() <= 10000,
            "Model placement exceeds the scene entity profile or has an invalid root");
    for (const auto& entity : candidate.entities)
        document["entities"].push_back(entity);
    detail::promote_scale_format(document, "entities", 5);
    require(document.dump().size() <= 8 * 1024 * 1024,
            "Model placement exceeds the scene byte profile");
    scene.edit(document);
    return candidate.root;
}
EntityId instantiate_mesh(Scene& scene, const AssetCatalog& catalog, AssetRef<MeshAsset> mesh,
                          LocalTranslation position, const std::string& name) {
    const auto resolved = catalog.resolve(mesh);
    if (resolved.state != AssetState::Available)
        throw std::runtime_error("Mesh placement rejected: " + resolved.diagnostic);
    auto document =
        preview_authoring(scene, Json::array({{{"operation", "entity.create"},
                                               {"arguments",
                                                {{"recipe", "render.mesh"},
                                                 {"name", name},
                                                 {"position", detail::encode(position)}}}}}));
    auto& entity = document["entities"].back();
    entity["components"]["forge.mesh_renderer"]["mesh"] = mesh.id;
    const auto id = entity.at("id").get<EntityId>();
    scene.edit(document);
    return id;
}
EntityId instantiate_prefab_at(Scene& scene, AssetId asset, LocalTranslation position) {
    detail::SceneDraft draft(scene);
    const auto id = draft.instantiate_prefab(asset);
    const auto effective = draft.effective_document();
    AffineTransform desired;
    for (const auto& entity : effective.at("entities"))
        if (entity.at("id") == id) {
            require(entity.value("spatial_resolved", false),
                    "Prefab placement has an unresolved spatial parent");
            desired.m = entity.at("world_affine").get<std::array<double, 12>>();
        }
    desired.m[3] = position.x;
    desired.m[7] = position.y;
    desired.m[11] = position.z;
    auto document = draft.document();
    detail::write_world(document, effective, id, desired, TransformChannel::Translation);
    require(document.at("entities").size() <= 10000 && document.dump().size() <= 8 * 1024 * 1024,
            "Prefab placement exceeds the scene authoring profile");
    scene.edit(document);
    return EntityId::parse(id);
}
} // namespace forge::asset_detail
