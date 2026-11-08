#pragma once
#include <forge/authoring.hpp>
#include <forge/prefab_authoring.hpp>
#include <forge/sound_cue.hpp>

inline void test_sound_playlist_authoring() {
    using namespace forge;
    const auto require = [](bool valid, const char* text) {
        if (!valid)
            throw std::runtime_error(text);
    };
    EngineContext engine;
    Scene scene(engine.world());
    const auto a = AssetId::generate(), b = AssetId::generate();
    const Json clips = Json::array(
        {{{"clip", a}, {"name", "First"}}, {{"clip", b}}, {{"clip", a}, {"name", "Again"}}});
    const Json arguments{{"name", "Three tracks"},
                         {"clips", clips},
                         {"gap_seconds", .125},
                         {"pause_with_game", false}};
    const Json commands =
        Json::array({{{"operation", "audio.playlist_create"}, {"arguments", arguments}}});
    const auto before = scene.snapshot();
    const auto revision = scene.revision();
    const auto preview = preview_authoring(scene, commands);
    require(scene.snapshot() == before && scene.revision() == revision &&
                preview.at("entities").size() == 4,
            "Playlist preview published objects or omitted part of its composition");
    const auto created = apply_authoring(scene, commands, revision).at("results")[0];
    const auto root = created.at("selected").get<std::string>();
    const auto sources = created.at("sources").get<std::vector<std::string>>();
    const auto cue = scene.entity(root).get<SoundCue>();
    require(sources.size() == 3 && scene.entity_count() == 4 && cue.nodes.size() == 4 &&
                cue.nodes[0].kind == 3 && cue.repeat_mode == 1 && cue.repeat_on_start &&
                cue.repeat_interval_seconds == .125 && cue.repeat_event_limit == 3 &&
                !cue.repeat_pause_with_game,
            "Playlist did not use the existing ordered/completion/finite cue values");
    for (std::size_t i = 0; i < sources.size(); ++i) {
        const auto entity = scene.entity(sources[i]);
        const auto sound = entity.get<AudioSource>();
        require(cue.nodes[0].children[i].target == i + 1 &&
                    cue.nodes[i + 1].source == scene.reference(sources[i]) &&
                    sound.clip.id == clips[i].at("clip").get<AssetId>() && !sound.play_on_start &&
                    !sound.loop && !sound.spatialized && !sound.pause_with_game &&
                    entity.parent() == scene.entity(root),
                "Playlist reordered clips, duplicated playback intent or escaped its subtree");
    }
    const auto after = scene.snapshot();
    require(scene.undo() && scene.snapshot() == before && scene.redo() && scene.snapshot() == after,
            "Playlist creation was not one Undo/Redo transaction with stable identities");
    const auto captured = create_prefab_source(scene, root);
    require(captured.source.at("version") == 3 && captured.source.at("members").size() == 4,
            "Playlist could not use the existing portable Prefab capture owner");
    {
        EngineContext other;
        Scene reused(other.world());
        reused.set_prefab_sources({{captured.asset(), captured.source}});
        const auto left = instantiate_prefab(reused, captured.asset());
        const auto right = instantiate_prefab(reused, captured.asset());
        const auto first_copy = reused.entity(left).get<SoundCue>();
        const auto second_copy = reused.entity(right).get<SoundCue>();
        for (std::size_t i = 1; i < first_copy.nodes.size(); ++i)
            require(first_copy.nodes[i].source.scene == reused.asset_id() &&
                        first_copy.nodes[i].source != second_copy.nodes[i].source &&
                        reused.entity(first_copy.nodes[i].source.entity.str()).parent() ==
                            reused.entity(left) &&
                        reused.entity(second_copy.nodes[i].source.entity.str()).parent() ==
                            reused.entity(right),
                    "Portable playlist instances shared Sources or retained their capture Scene");
    }
    const auto duplicate = scene.duplicate_subtree(root);
    const auto copied = scene.entity(duplicate).get<SoundCue>();
    for (std::size_t i = 1; i < copied.nodes.size(); ++i)
        require(copied.nodes[i].source != cue.nodes[i].source &&
                    scene.entity(copied.nodes[i].source.entity.str()).parent() ==
                        scene.entity(duplicate),
                "Duplicating a playlist retained another instance's Source references");

    const auto reject = [&](const Json& value) {
        const auto saved = scene.snapshot();
        const auto rev = scene.revision();
        bool failed = false;
        try {
            (void)authoring_command(scene, "audio.playlist_create", value);
        } catch (const std::exception&) {
            failed = true;
        }
        require(failed && scene.snapshot() == saved && scene.revision() == rev,
                "Invalid playlist leaked a partial Source/cue or history change");
    };
    auto invalid = arguments;
    invalid["clips"][2]["clip"] = "not-an-asset-id";
    reject(invalid);
    invalid = arguments;
    invalid["clips"][2]["name"] = "   ";
    reject(invalid);
    invalid = arguments;
    invalid["clips"] = Json::array();
    reject(invalid);
    invalid["clips"] = Json::array();
    for (unsigned i = 0; i <= sound_cue_child_limit; ++i)
        invalid["clips"].push_back({{"clip", a}});
    reject(invalid);
    invalid = arguments;
    invalid["gap_seconds"] = -1;
    reject(invalid);
    bool stale = false;
    try {
        (void)apply_authoring(scene, commands, revision);
    } catch (const AuthoringError& error) {
        stale = error.code == "stale_revision";
    }
    require(stale, "Playlist creation bypassed the existing revision guard");

    Json maximum = Json::array();
    for (unsigned i = 0; i < sound_cue_child_limit; ++i)
        maximum.push_back({{"clip", i % 2 ? a : b}});
    const auto loop =
        authoring_command(scene, "audio.playlist_create",
                          {{"clips", maximum}, {"loop", true}, {"play_on_start", false}});
    const auto looping = scene.entity(loop.at("selected").get<std::string>()).get<SoundCue>();
    require(looping.nodes.size() == sound_cue_child_limit + 1 &&
                looping.nodes[0].children.size() == sound_cue_child_limit &&
                looping.repeat_event_limit == 0 && !looping.repeat_on_start &&
                looping.repeat_pause_with_game,
            "Maximum looping playlist changed graph bounds, startup or pause defaults");
}
