#pragma once

void sound_cue_prefab_tests(const std::filesystem::path& root) {
    const auto project = root / "sound-cue-prefabs";
    std::filesystem::create_directories(project);
    EngineContext engine;
    Scene scene(engine.world());
    const auto create = [&](const char* name, const char* recipe, Json values = Json::object()) {
        Json arguments{{"name", name}, {"recipe", recipe}};
        if (!values.empty())
            arguments["component_values"] = std::move(values);
        return authoring_command(scene, "entity.create", arguments)
            .at("selected")
            .get<std::string>();
    };
    const auto group = create("Rig", "empty");
    const auto a = create("Sound A", "audio.source", {{"clip", AssetId::generate()}});
    const auto b = create("Sound B", "audio.source", {{"clip", AssetId::generate()}});
    const auto cue = create("Event", "audio.sound_cue");
    SoundCue graph{true,
                   23,
                   0,
                   {{2, {}, {{1, 2}, {2, 7}}},
                    {0, scene.reference(a), {}, .125},
                    {0, scene.reference(b), {}, .375}}};
    graph.repeat_interval_seconds = .75;
    graph.repeat_mode = 1;
    graph.repeat_on_start = true;
    auto authored_graph = sound_cue_json(graph);
    const Json opaque_reference = scene.reference(a);
    authored_graph["nodes"][1]["future"] = {{"source", opaque_reference}};
    authoring_command(
        scene, "property.set_many",
        {{"entity", cue}, {"component", "forge.sound_cue"}, {"values", authored_graph}});
    for (const auto& id : {a, b, cue})
        scene.reparent_entity(id, group, ReparentMode::KeepLocal);
    const auto duplicate = scene.duplicate_subtree(group);
    const auto verify = [&](const Scene& target, const std::string& parent) {
        const auto event_id = named(target, "Event", parent);
        const auto actual = target.entity(event_id).get<SoundCue>();
        check(actual.nodes[1].source == target.reference(named(target, "Sound A", parent)) &&
                  actual.nodes[2].source == target.reference(named(target, "Sound B", parent)) &&
                  actual.nodes[0].children == graph.nodes[0].children &&
                  actual.seed == graph.seed && actual.nodes[1].delay_seconds == .125 &&
                  actual.nodes[2].delay_seconds == .375 && actual.repeat_interval_seconds == .75 &&
                  actual.repeat_on_start && actual.repeat_pause_with_game &&
                  actual.repeat_mode == 1,
              "Nested Sound Cue references crossed copied/Prefab instance ownership");
        const auto effective = target.effective_document();
        const auto& event_row =
            *std::find_if(effective.at("entities").begin(), effective.at("entities").end(),
                          [&](const auto& row) { return row.at("id") == event_id; });
        check(event_row.at("components")
                      .at("forge.sound_cue")
                      .at("nodes")[1]
                      .at("future")
                      .at("source") == opaque_reference,
              "Opaque nested Cue data was dropped or mistaken for an admitted entity reference");
    };
    verify(scene, duplicate);
    const auto captured = create_prefab_source(scene, group);
    check(captured.source.at("version") == 3,
          "Sound Cue graph omitted portable Prefab member links");
    PrefabLibrary library(project);
    const auto asset = library.create(scene, captured, "Cue.prefab.json");
    const auto first = instantiate_prefab(scene, asset), second = instantiate_prefab(scene, asset);
    verify(scene, first);
    verify(scene, second);
    const auto event = named(scene, "Event", first);
    authoring_command(
        scene, "property.set",
        {{"entity", event}, {"component", "forge.sound_cue"}, {"field", "seed"}, {"value", 31}});
    check(scene.entity(event).get<SoundCue>().seed == 31 &&
              scene.entity(named(scene, "Event", second)).get<SoundCue>().seed == graph.seed,
          "Cue property override escaped its instance");
    check(scene.undo() && scene.entity(event).get<SoundCue>().seed == graph.seed && scene.redo() &&
              scene.entity(event).get<SoundCue>().seed == 31,
          "Cue instance override lost explicit Scene history");
    EngineContext reopened_engine;
    Scene reopened(reopened_engine.world());
    reopened.set_prefab_sources(scene.prefab_sources());
    reopened.restore_snapshot(scene.snapshot());
    verify(reopened, second);
    check(reopened.entity(event).get<SoundCue>().seed == 31,
          "Saved native Cue graph lost its independent override");
}
