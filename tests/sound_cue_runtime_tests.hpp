#pragma once
#include <forge/sound_cue.hpp>

void sound_cue_runtime_tests(const std::filesystem::path& root, AssetId clip) {
    auto doc = source(clip);
    doc["entities"][0]["components"]["forge.audio_source"]["play_on_start"] = false;
    auto other = doc["entities"][0];
    other["id"] = "other";
    doc["entities"].push_back(other);
    doc["entities"].push_back({{"id", "cue"},
                               {"name", "Sound Cue"},
                               {"components", {{"forge.sound_cue", sound_cue_json({})}}}});
    Fixture single(root), layer(root);
    single.load(doc);
    layer.load(doc);
    const auto ref = layer.ref("cue"), a = layer.ref(), b = layer.ref("other");
    SoundCue graph{true, 7, 0, {{1, {}, {{1, 1}, {2, 1}}}, {0, a, {}}, {0, b, {}}}};
    layer.scene.entity("cue").set(graph);
    const auto authored = layer.scene.snapshot();
    const auto tick = layer.audio->status().at("tick");
    const auto pcm_bytes = layer.audio->status().at("decoded_bytes");
    single.audio->play_one_shot(single.ref());
    layer.audio->play_one_shot(ref);
    check(AudioSourceAccess::status(*layer.audio, ref).at("accepted_triggers") == 1 &&
              layer.audio->status().at("one_shots") == 0,
          "Cue did not retain enqueue-only selection or bypassed the fixed owner boundary");
    single.tick();
    layer.tick();
    check(layer.audio->status().at("one_shots") == 2 &&
              layer.audio->status().at("decoded_bytes") == pcm_bytes,
          "Cue did not layer two existing-source voices or duplicated PCM");
    single.audio->paused(false);
    layer.audio->paused(false);
    (void)single.energy(4096);
    (void)layer.energy(4096);
    const auto ratio = layer.energy(4800) / single.energy(4800);
    check(ratio > 3.98 && ratio < 4.02, "Two-leaf cue PCM did not sum independently");
    check(layer.scene.snapshot() == authored && layer.audio->status().at("tick") != tick,
          "Cue trigger changed authored state");
    reject([&] { layer.audio->stop_source(ref); });
    reject([&] { layer.audio->play(ref); });
    reject([&] { layer.audio->pause_source(ref); });
    reject([&] { layer.audio->resume_source(ref); });
    const auto accepted = AudioSourceAccess::status(*layer.audio, ref);
    // Even an unselected leaf is validated; rejection preserves sequence position.
    graph.nodes[0].kind = 3;
    graph.nodes[2].source = {ref.scene, EntityId::generate()};
    layer.scene.entity("cue").set(graph);
    reject([&] { layer.audio->play_one_shot(ref); });
    graph.nodes[2].source = b;
    layer.scene.entity("cue").set(graph);
    layer.audio->play_one_shot(ref);
    auto status = AudioSourceAccess::status(*layer.audio, ref);
    check(status.at("accepted_triggers") == 1 && status.at("last_sources") == Json::array({a}),
          "Changed cue did not reset or rejected leaf advanced its ordered choice");
    layer.audio->play_one_shot(ref);
    check(AudioSourceAccess::status(*layer.audio, ref).at("last_sources") == Json::array({b}),
          "Ordered cue did not select its next source");
    // Control-domain dispatch admits the whole definition, not just today's branch.
    auto value = layer.scene.entity("speaker").get<AudioSource>();
    value.pause_with_game = false;
    layer.scene.entity("speaker").set(value);
    reject([&] { layer.audio->control_unpaused_source(ref, AudioSourceOperation::OneShot); });
    value = layer.scene.entity("other").get<AudioSource>();
    value.pause_with_game = false;
    layer.scene.entity("other").set(value);
    layer.tick();
    layer.audio->control_unpaused_source(ref, AudioSourceOperation::OneShot);
    check(AudioSourceAccess::status(*layer.audio, ref).at("accepted_triggers") == 3,
          "Complete control-domain cue did not dispatch through existing one-shots");
    check(accepted.at("maximum_outputs") == 2, "Cue observation lost its bounded fanout");

    Fixture atomic(root);
    atomic.load(doc);
    const auto cue_ref = atomic.ref("cue");
    graph = {true,
             1,
             0,
             {{1, {}, {{1, 1}, {2, 1}}}, {0, atomic.ref(), {}}, {0, atomic.ref("other"), {}}}};
    atomic.scene.entity("cue").set(graph);
    for (unsigned i = 0; i < 16; ++i)
        atomic.audio->play_one_shot(atomic.ref("other"));
    reject([&] { atomic.audio->play_one_shot(cue_ref); });
    check(AudioSourceAccess::status(*atomic.audio, cue_ref).at("accepted_triggers") == 0,
          "Partially rejected cue advanced selection state");
    atomic.tick();
    check(AudioSourceAccess::status(*atomic.audio, atomic.ref()).at("one_shots") == 0 &&
              atomic.audio->status().at("one_shots") == 16,
          "Rejected cue left a partial first-source command in the shared queue");
    // Complete native and JSON copying retain nested typed references and graph values.
    auto saved = atomic.scene.snapshot();
    atomic.load(saved);
    const auto restored = atomic.scene.entity("cue").get<SoundCue>();
    check(restored == graph &&
              AudioSourceAccess::status(*atomic.audio, atomic.ref("cue")).at("accepted_triggers") ==
                  0,
          "Cue native values or fresh-world selection semantics changed after copying");
}
