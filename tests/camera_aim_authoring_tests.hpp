#pragma once
// Included beside the Camera Aim fixture and its independent view oracles.
void authoring_and_instances(Fixture& f, const std::filesystem::path& scratch) {
    CameraFollow follow;
    follow.enabled = false;
    follow.target = {f.scene.asset_id(), EntityId::parse(f.target)};
    f.scene.entity(f.view).set(follow);
    auto source = f.scene.document();
    row(source, f.view)["components"]["forge.camera_follow"]["future_follow"] = {
        {"identity_like_text", f.target}, {"kept", true}};
    row(source, f.view)["components"]["forge.camera_aim"]["future_aim"] = {
        {"identity_like_text", f.target}, {"kept", true}};
    f.scene.edit(source);
    const auto before = f.scene.document();
    authoring_command(f.scene, "property.set_many",
                      {{"entity", f.view},
                       {"component", "forge.camera_aim"},
                       {"values", {{"weight", .5}, {"offset_y", .25}}}});
    auto after = f.scene.document();
    check(f.scene.entity(f.view).get<CameraAim>().weight == .5 &&
              row(source, f.view)["components"]["forge.camera_aim"]["future_aim"] ==
                  row(after, f.view)["components"]["forge.camera_aim"]["future_aim"],
          "Camera Aim field transaction discarded opaque data");
    check(f.scene.undo() && f.scene.document() == before && f.scene.redo() &&
              f.scene.document() == after && f.scene.undo() && f.scene.document() == before,
          "Camera Aim fields did not retain one ordinary history step");
    authoring_command(f.scene, "property.set_many",
                      {{"entity", f.view},
                       {"component", "forge.camera_follow"},
                       {"values", {{"offset_space", 1u}, {"weight", .25}, {"offset_x", 1.5}}}});
    auto followed = f.scene.document();
    check(f.scene.entity(f.view).get<CameraFollow>().offset_space == 1 &&
              row(after, f.view)["components"]["forge.camera_follow"]["future_follow"] ==
                  row(followed, f.view)["components"]["forge.camera_follow"]["future_follow"] &&
              f.scene.undo() && f.scene.document() == before && f.scene.redo() &&
              f.scene.document() == followed && f.scene.undo() && f.scene.document() == before,
          "Camera Follow field transaction lost history or opaque data");
    const auto root = authoring_command(f.scene, "entity.create", {{"recipe", "empty"}})
                          .at("selected")
                          .get<std::string>();
    f.scene.entity(root).set<LocalTranslation>({0, 0, 0});
    for (const auto& id : {f.view, f.target})
        f.scene.reparent_entity(id, root, ReparentMode::KeepLocal);
    const auto before_copy = f.scene.document();
    const auto copy = f.scene.duplicate_subtree(root);
    auto copied = f.scene.document();
    std::string copy_camera, copy_target;
    for (const auto& item : copied.at("entities"))
        if (item.value("parent", Json()) == Json(copy)) {
            if (item.at("components").contains("forge.camera_aim"))
                copy_camera = item.at("id");
            else
                copy_target = item.at("id");
        }
    check(!copy_camera.empty() && !copy_target.empty(),
          "Camera Aim duplicated subtree is incomplete");
    check(f.scene.entity(copy_camera).get<CameraAim>().target ==
                  EntityRef{f.scene.asset_id(), EntityId::parse(copy_target)} &&
              row(copied, copy_camera)["components"]["forge.camera_aim"]["future_aim"]
                                      ["identity_like_text"] == f.target,
          "Camera Aim duplication failed typed remapping or searched opaque identity-like text");
    check(f.scene.entity(copy_camera).get<CameraFollow>().target ==
                  EntityRef{f.scene.asset_id(), EntityId::parse(copy_target)} &&
              row(copied, copy_camera)["components"]["forge.camera_follow"]["future_follow"]
                                      ["identity_like_text"] == f.target,
          "Camera Follow duplication lost typed reference remapping or opaque text");
    check(f.scene.undo() && f.scene.document() == before_copy && f.scene.redo() &&
              f.scene.document() == copied,
          "Camera Aim subtree duplication changed identities across history");
    const auto definition = create_prefab_source(f.scene, root);
    PrefabMemberId camera_member, target_member;
    for (const auto& member : definition.source.at("members"))
        if (member.at("components").contains("forge.camera_aim")) {
            camera_member = member.at("id").get<PrefabMemberId>();
            const auto& reference = member.at("components").at("forge.camera_aim").at("target");
            check(reference.at("prefab") == Json(definition.asset()),
                  "Camera Aim Prefab capture did not use the definition reference owner");
            target_member = reference.at("member").get<PrefabMemberId>();
            check(member.at("components").at("forge.camera_follow").at("target") == reference,
                  "Camera Follow Prefab capture differs from the shared reference owner");
        }
    check(bool(camera_member) && bool(target_member), "Camera Aim Prefab references are missing");
    const PrefabSources definitions{{definition.asset(), definition.source}};
    f.scene.set_prefab_sources(definitions);
    std::array<std::pair<EntityRef, EntityRef>, 2> bindings;
    for (auto& binding : bindings) {
        const auto instance = instantiate_prefab(f.scene, definition.asset());
        const auto doc = f.scene.document();
        const auto camera_ref = prefab_member_reference(doc, EntityId::parse(instance),
                                                        {definition.asset(), camera_member});
        const auto target_ref = prefab_member_reference(doc, EntityId::parse(instance),
                                                        {definition.asset(), target_member});
        check(camera_ref && target_ref, "Camera Aim instance references did not resolve");
        binding = {*camera_ref, *target_ref};
        check(f.scene.entity(camera_ref->entity.str()).get<CameraAim>().target == *target_ref,
              "Camera Aim instance retained a definition or another instance's target");
        check(f.scene.entity(camera_ref->entity.str()).get<CameraFollow>().target == *target_ref,
              "Camera Follow instance retained another scope's target");
    }
    check(bindings[0].second != bindings[1].second, "Camera Aim instances share a target identity");
    authoring_command(f.scene, "property.set",
                      {{"entity", bindings[0].second.entity.str()},
                       {"component", "forge.local_translation"},
                       {"field", "x"},
                       {"value", 20.}});
    const auto render = extract_render_scene(f.scene.effective_document());
    check(render.diagnostics.empty(), "Camera Aim instance projection produced a diagnostic");
    near(camera_view(camera(render, bindings[0].first.entity.str()).camera,
                     camera(render, bindings[0].first.entity.str()).world, 800, 600)
             .forward,
         {20 / std::sqrt(404.), 2 / std::sqrt(404.), 0});
    near(camera_view(camera(render, bindings[1].first.entity.str()).camera,
                     camera(render, bindings[1].first.entity.str()).world, 800, 600)
             .forward,
         {10 / std::sqrt(104.), 2 / std::sqrt(104.), 0});
    std::filesystem::create_directories(scratch);
    f.scene.save(scratch / "camera-aim.scene.json");
    EngineContext reopened_engine;
    Scene reopened(reopened_engine.world());
    reopened.set_prefab_sources(definitions);
    reopened.load(scratch / "camera-aim.scene.json");
    check(reopened.document() == f.scene.document(),
          "Camera Aim save/reopen changed authored data");
    const auto restored = extract_render_scene(reopened.effective_document());
    near(camera_view(camera(restored, bindings[0].first.entity.str()).camera,
                     camera(restored, bindings[0].first.entity.str()).world, 800, 600)
             .forward,
         {20 / std::sqrt(404.), 2 / std::sqrt(404.), 0});
}
