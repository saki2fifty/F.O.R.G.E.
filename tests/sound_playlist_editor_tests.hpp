#pragma once
#include "audio_test_fixture.hpp"
#include "imgui_input_driver.hpp"
#include "imgui_software_capture.hpp"
#include "sound_playlist.hpp"

inline void test_sound_playlist_controls(const std::filesystem::path& scratch) {
    using namespace forge;
    std::filesystem::create_directories(scratch);
    require(SDL_Init(SDL_INIT_VIDEO), SDL_GetError());
    struct Quit {
        ~Quit() { SDL_Quit(); }
    } quit;
    for (float scale : {1.f, 1.5f, 2.f}) {
        const auto root = scratch / AssetId::generate().str();
        SceneDocument::create_project(root, "Playlist controls");
        wav(root / "first.wav");
        wav(root / "second.wav");
        const auto first = AssetCatalog::register_audio_clip(root, "first.wav");
        const auto second = AssetCatalog::register_audio_clip(root, "second.wav");
        auto records = AssetCatalog::open_project(root).records();
        SoundPlaylistEditor::Lookup lookup = [&](AssetId id) -> const AssetRecord* {
            const auto found = records.find(id);
            return found == records.end() ? nullptr : &found->second;
        };
        EngineContext engine;
        Scene scene(engine.world());
        SceneDocument project(scene);
        project.open_project(root);
        test::ImGuiSoftwareCapture capture(int(700 * scale), int(800 * scale));
        ui::style(scale);
        ImGui::GetIO().ConfigInputTrickleEventQueue = false;
        ui::EditorUiContext owner;
        owner.scene = &scene;
        ui::ContextScope context(owner);
        test::observe_ui = true;
        SoundPlaylistEditor playlist;
        SoundCueEditor graph;
        bool editing = false, locked = false;
        std::string selected;
        auto frame = [&] {
            test::ui_targets.clear();
            capture.new_frame();
            if (editing) {
                ImGui::SetNextWindowPos({0, 0}, ImGuiCond_Always);
                ImGui::SetNextWindowSize(ImGui::GetIO().DisplaySize, ImGuiCond_Always);
                ImGui::Begin("Sound Cue", nullptr, ImGuiWindowFlags_NoResize);
                graph.draw(scene, selected, sound_cue_json(scene.entity(selected).get<SoundCue>()),
                           path_text(root));
                ImGui::End();
            }
            playlist.draw(scene, project, lookup, locked,
                          [&](const std::string& id) { selected = id; });
            capture.render();
        };
        test::ImGuiInputDriver input(frame);
        playlist.open(scene, project, {first.id, second.id}, lookup);
        frame();
        frame();
        const auto initial = scene.snapshot();
        input.click("sound-playlist:down:0");
        input.type("sound-playlist:name", "Two tracks");
        input.type("sound-playlist:gap", "0.25");
        input.click("sound-playlist:startup");
        require(scene.snapshot() == initial, "Playlist draft mutated authored Scene");
        if (scale == 1)
            capture.save(scratch / "sound-playlist-create.png");
        locked = true;
        frame();
        require(!test::ui_targets.at("sound-playlist:create").enabled,
                "Busy playlist creation remained enabled");
        locked = false;
        authoring_command(scene, "entity.create", {{"name", "Unrelated change"}});
        const auto before = scene.snapshot();
        frame();
        require(!test::ui_targets.at("sound-playlist:create").enabled,
                "Stale playlist creation remained enabled");
        input.click("sound-playlist:refresh");
        input.click("sound-playlist:create");
        require(!playlist.active() && !selected.empty(), "Playlist controls did not create a cue");
        const auto cue = scene.entity(selected).get<SoundCue>();
        require(cue.repeat_event_limit == 2 && cue.repeat_mode == 1 &&
                    cue.repeat_interval_seconds == .25 && !cue.repeat_on_start,
                "Playlist controls lost finite count, gap or startup policy");
        const auto source_clip = [&](unsigned index) {
            return scene.entity(cue.nodes[index].source.entity.str()).get<AudioSource>().clip.id;
        };
        require(source_clip(1) == second.id && source_clip(2) == first.id,
                "Playlist move controls did not preserve requested clip order");
        const auto created = scene.snapshot();
        require(scene.undo() && scene.snapshot() == before && scene.redo() &&
                    scene.snapshot() == created,
                "Playlist controls did not publish one stable Undo/Redo transaction");
        editing = true;
        input.click("sound-cue:edit");
        frame();
        if (!test::ui_targets.contains("sound-cue:down:0:0"))
            input.click("sound-cue:node:0");
        input.click("sound-cue:down:0:0");
        require(scene.snapshot() == created, "Graph move published before Apply");
        input.click("sound-cue:apply");
        require(scene.entity(selected).get<SoundCue>().nodes[0].children[0].target == 2 &&
                    scene.undo() && scene.snapshot() == created,
                "Ordered graph move did not apply through one Undo step");
        editing = false;
        playlist.open(scene, project, {first.id, second.id}, lookup);
        frame();
        frame();
        records.erase(second.id);
        frame();
        require(!test::ui_targets.at("sound-playlist:create").enabled,
                "Missing clip did not invalidate the playlist draft");
        input.click("sound-playlist:cancel");
        require(!playlist.active() && scene.snapshot() == created,
                "Cancel published an invalid playlist draft");
        records.emplace(second.id, second);
        playlist.open(scene, project, std::vector<AssetId>(8, first.id), lookup);
        frame();
        frame();
        input.click("sound-playlist:remove:7");
        input.click("sound-playlist:loop");
        input.click("sound-playlist:game-pause");
        input.click("sound-playlist:create");
        const auto looping = scene.entity(selected).get<SoundCue>();
        require(looping.nodes.size() == 8 && looping.repeat_event_limit == 0 &&
                    !looping.repeat_pause_with_game &&
                    !scene.entity(looping.nodes[1].source.entity.str())
                         .get<AudioSource>()
                         .pause_with_game &&
                    scene.undo() && scene.snapshot() == created,
                "Scrolled maximum-list controls lost removal, loop, pause or atomic history");
        playlist.open(scene, project, {first.id}, lookup);
        frame();
        project.open_project(root);
        const auto reopened = scene.snapshot();
        frame();
        require(!test::ui_targets.at("sound-playlist:create").enabled,
                "Playlist survived document-generation retirement");
        input.click("sound-playlist:cancel");
        require(scene.snapshot() == reopened,
                "Cancel after document retirement changed the new Scene");
        test::observe_ui = false;
    }
}
