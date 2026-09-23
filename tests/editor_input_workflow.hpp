#pragma once
#include "ui_probe.hpp"
#include <SDL3/SDL.h>
#include <bit>
#include <cmath>
#include <forge/scene.hpp>
#include <fstream>
#include <functional>
#include <stdexcept>

namespace forge::test {
// Scene state is observed, never edited through commands or model APIs.
// Inputs drive authoring; raw files supply a DCC import/reimport scenario.
class EditorInputWorkflow {
    enum class Kind { Click, Hover, Text, Key, Check, Capture, DropFile, SourceEdit };
    struct Step {
        Kind kind;
        std::string value;
        ImGuiKey key = ImGuiKey_None;
        bool control = false;
    };
    std::vector<Step> steps_;
    std::size_t index_ = 0;
    unsigned frame_ = 0;
    Uint64 since_ = 0;
    ImVec2 pointer_{-FLT_MAX, -FLT_MAX};
    std::string failure_, last_check_, cube_, camera_, light_, scene_;
    std::uint64_t paused_tick_ = 0;
    Json cache_scene_, saved_, before_model_, trace_ = Json::array();
    std::filesystem::path external_source_, project_;
    std::string drop_path_, model_asset_, model_root_;
    std::uint64_t model_generation_ = 0;
    static Json model_source(bool changed = false) {
        auto source = Json::parse(R"({"asset":{"version":"2.0"},
            "extensionsUsed":["KHR_materials_unlit"],
            "buffers":[{"uri":"workflow.bin","byteLength":36}],
            "bufferViews":[{"buffer":0,"byteLength":36}],
            "accessors":[{"bufferView":0,"componentType":5126,"count":3,"type":"VEC3",
                "min":[-1,-1,0],"max":[1,1,0]}],
            "materials":[{"name":"Workflow surface","doubleSided":true,
                "extensions":{"KHR_materials_unlit":{}},
                "pbrMetallicRoughness":{"baseColorFactor":[0.8,0.2,0.04,1]}}],
            "meshes":[{"name":"Workflow triangle","primitives":[{"attributes":{"POSITION":0},"material":0}]}],
            "nodes":[{"name":"Imported triangle","mesh":0}],"scenes":[{"nodes":[0]}],"scene":0})");
        if (changed)
            source["materials"][0]["pbrMetallicRoughness"]["baseColorFactor"] = {.05, .8, .25, 1};
        return source;
    }
    static void write(const std::filesystem::path& path, const std::string& bytes) {
        std::ofstream file(path, std::ios::binary | std::ios::trunc);
        file.write(bytes.data(), std::streamsize(bytes.size()));
        file.close();
        if (!file)
            throw std::runtime_error("Cannot write external workflow source");
    }
    void click(std::string target, bool ctrl = false) {
        steps_.push_back({Kind::Click, std::move(target), ImGuiKey_None, ctrl});
    }
    void hover(std::string target) { steps_.push_back({Kind::Hover, std::move(target)}); }
    void check(std::string what) { steps_.push_back({Kind::Check, std::move(what)}); }
    void capture(std::string name) { steps_.push_back({Kind::Capture, std::move(name)}); }
    void key(ImGuiKey key, bool ctrl = false) { steps_.push_back({Kind::Key, {}, key, ctrl}); }
    void text(std::string target, std::string value, bool ctrl = false) {
        click(std::move(target), ctrl);
        steps_.push_back({Kind::Text, std::move(value)});
    }
    void create(const std::string& category, const std::string& recipe) {
        click("menu:Entity");
        hover("menu:Create");
        hover("category:" + category);
        capture("create-" + recipe + "-menu");
        click("action:Create / " + recipe);
    }
    static void require(bool ok, const char* reason) {
        if (!ok)
            throw std::runtime_error(reason);
    }
    void verify(const std::string& what, const Json& state) {
        const auto& doc = state.at("scene");
        const auto& entities = doc.at("entities");
        if (what == "source-imported") {
            require(state.at("source_imported").get<bool>(),
                    "Source import did not publish its model");
            project_ = std::filesystem::u8path(state.at("project").get<std::string>());
            before_model_ = doc;
        } else if (what == "model-ready") {
            require(state.at("model_ready").get<bool>() && !state.at("model_asset").is_null(),
                    "Model settings/preview have not loaded the published source");
            model_asset_ = state.at("model_asset");
            model_generation_ = state.at("model_generation");
            require(doc == before_model_, "Import/reimport changed scene history/state");
        } else if (what == "model-placed") {
            if (model_root_.empty())
                model_root_ = state.at("selected");
            require(
                entities.size() == before_model_.at("entities").size() + 2 &&
                    std::any_of(entities.begin(), entities.end(),
                                [&](const auto& entity) {
                                    return entity.at("id") == model_root_ &&
                                           entity.at("components").contains("forge.model_source");
                                }),
                "Place configured model did not create its root and mesh node");
        } else if (what == "model-undone") {
            require(doc == before_model_, "Scene Undo did not remove complete model placement");
        } else if (what == "hot-reimport-rejected") {
            const auto& failures = state.at("failed_imports");
            require(std::find(failures.begin(), failures.end(), model_asset_) != failures.end(),
                    "Corrupt source was not reported as a failed import");
            require(state.at("model_ready").get<bool>() &&
                        state.at("model_asset") == model_asset_ && doc == saved_ &&
                        state.at("disk") == saved_,
                    "Corrupt source discarded the usable model or changed authored state");
            const auto& problems = state.at("problems");
            require(std::any_of(problems.begin(), problems.end(),
                                [&](const auto& problem) {
                                    return problem.at("asset") == model_asset_ &&
                                           problem.at("source") ==
                                               "Assets/Imported/Source-1/workflow.gltf";
                                }),
                    "Automatic reimport error has no navigable asset/source context");
        } else if (what == "failed-asset-selected") {
            require(state.at("selected_asset") == model_asset_ && doc == saved_,
                    "Selecting the reimport diagnostic did not inspect its asset safely");
        } else if (what == "hot-reimported") {
            require(state.at("model_ready").get<bool>() &&
                        state.at("model_asset") == model_asset_ &&
                        state.at("model_generation").get<std::uint64_t>() > model_generation_,
                    "Changed source did not update the open model while retaining AssetId");
            require(doc == saved_ && state.at("disk") == saved_,
                    "Model source publication mutated authored scene or saved scene");
        } else if (what == "before-cache")
            cache_scene_ = doc;
        else if (what == "cache-complete") {
            require(ui_targets.contains("cache:complete"), "Cache maintenance not complete");
            require(doc == cache_scene_, "Cache maintenance changed the authored scene");
        } else if (what == "empty")
            require(entities.empty(), "Workflow must start in an empty scene");
        else if (what == "picked-camera" || what == "picked-light") {
            require(state.at("selected") == (what == "picked-camera" ? camera_ : light_),
                    "Scene helper click did not select its entity");
        } else if (what == "cube") {
            require(entities.size() == 1 && !state.at("selected").get<std::string>().empty(),
                    "Menu did not create and select a cube");
            cube_ = state.at("selected");
        } else if (what == "rename")
            require(entities.at(0).at("name") == "Workflow cube", "Name field did not commit");
        else if (what == "position")
            require(std::abs(state.at("selected_preview")
                                 .at("components")
                                 .at("forge.position")
                                 .at("x")
                                 .get<double>() -
                             1.5) < .0001,
                    "Position field did not commit 1.5");
        else if (what == "scale" || what == "undo-scale") {
            const auto& c = state.at("selected_preview").at("components");
            const double y =
                c.contains("forge.scale") ? c.at("forge.scale").at("y").get<double>() : 1;
            require(std::abs(y - (what == "scale" ? 1.75 : 1)) < .0001,
                    "Scale field/history did not produce the expected value");
        } else if (what == "saved") {
            require(!state.at("dirty").get<bool>(), "Ctrl+S did not save the scene");
            saved_ = doc;
            scene_ = doc.at("asset_id");
            require(state.at("disk") == doc, "Saved disk data differs from authored scene");
        } else if (what == "unsaved" || what == "cancelled-reload") {
            require(state.at("dirty").get<bool>() && doc != saved_ && state.at("disk") == saved_,
                    "Unsaved rename must differ from unchanged disk data");
            require(doc.at("entities").at(0).at("name") == "Unsaved rename",
                    "Cancelled reload must preserve the unsaved name");
            if (what == "cancelled-reload")
                require(!ui_targets.contains("unsaved:discard"), "Cancel did not close the guard");
        } else if (what == "unsaved-guard") {
            require(ui_targets.contains("unsaved:discard") && ui_targets.contains("unsaved:cancel"),
                    "Reload of a dirty scene must ask before discarding it");
        } else if (what == "reload") {
            require(doc == saved_ && !state.at("dirty").get<bool>(),
                    "Reload from disk did not preserve the saved scene");
        } else if (what == "deleted")
            require(entities.empty(), "Delete menu did not delete selected cube");
        else if (what == "restored")
            require(doc == saved_, "Undo did not restore complete cube state");
        else if (what == "camera" || what == "light") {
            if (what == "camera")
                camera_ = state.at("selected");
            else
                light_ = state.at("selected");
            const auto& e = state.at("selected_preview");
            require(e.at("components").contains(what == "camera" ? "forge.camera" : "forge.light"),
                    "Rendering menu did not create/select the requested component");
            require(entities.size() == (what == "camera" ? 2u : 3u),
                    "Rendering menu created an unexpected entity count");
        } else if (what == "playing")
            require(state.at("playing").get<bool>() && state.at("control_ready").get<bool>() &&
                        !state.at("paused").get<bool>() && state.at("cameras").get<unsigned>() == 1,
                    "Play has not produced the authored game camera");
        else if (what == "paused") {
            require(state.at("paused").get<bool>() && state.at("control_ready").get<bool>(),
                    "Pause did not suspend Play");
            paused_tick_ = state.at("tick");
        } else if (what == "stepped") {
            require(state.at("paused").get<bool>() && state.at("control_ready").get<bool>() &&
                        state.at("tick").get<std::uint64_t>() == paused_tick_ + 1,
                    "Step must advance exactly one tick and remain paused");
        } else if (what == "zoom") {
            require(std::abs(state.at("ui_scale").get<double>() - 1.5) < .01,
                    "Ctrl+Plus did not produce 150% UI scale");
        } else if (what == "no-domain-errors") {
            for (const auto& problem : state.at("problems")) {
                const auto severity = problem.at("severity").get<std::string>();
                if (severity == "error" || severity == "fatal" || severity == "Error" ||
                    severity == "Fatal")
                    throw std::runtime_error("Unexpected editor diagnostic: " +
                                             problem.at("text").get<std::string>());
            }
            const auto* panel = ImGui::FindWindowByName("###Problems");
            require(panel && panel->Active && !panel->Hidden && panel->DockTabIsVisible,
                    "Click did not reveal the Problems panel");
        } else if (what == "stopped")
            require(!state.at("playing").get<bool>(), "Stop did not return to authoring");
    }

  public:
    explicit EditorInputWorkflow(bool enabled, const std::filesystem::path& evidence) {
        observe_ui = enabled;
        if (!enabled)
            return;
        external_source_ = evidence / "external-source" / "workflow.gltf";
        std::filesystem::create_directories(external_source_.parent_path());
        write(external_source_, model_source().dump());
        std::string vertices;
        for (float value : std::array<float, 9>{-1, -1, 0, 1, -1, 0, 0, 1, 0}) {
            const auto bits = std::bit_cast<std::uint32_t>(value);
            for (unsigned byte = 0; byte < 4; ++byte)
                vertices.push_back(char((bits >> (byte * 8)) & 255));
        }
        write(external_source_.parent_path() / "workflow.bin", vertices);
        drop_path_ = external_source_.string();
        key(ImGuiKey_0, true);
        check("empty");
        capture("empty-scene");
        create("3D Primitive", "Cube");
        check("cube");
        capture("created-cube");
        text("inspector:name", "Workflow cube");
        check("rename");
        text("transform:forge.position:0", "1.5", true);
        check("position");
        capture("edited-position");
        text("transform:forge.scale:1", "1.75", true);
        check("scale");
        capture("edited-scale");
        key(ImGuiKey_Z, true);
        check("undo-scale");
        capture("undo-scale");
        key(ImGuiKey_Y, true);
        check("scale");
        key(ImGuiKey_S, true);
        check("saved");
        capture("saved-scene");
        click("menu:Entity");
        click("action:Entity / Delete subtree");
        check("deleted");
        key(ImGuiKey_Z, true);
        check("restored");
        click("saved-cube-row");
        text("inspector:name", "Unsaved rename");
        check("unsaved");
        click("menu:File");
        capture("file-reload-menu");
        click("file:Reload from disk");
        check("unsaved-guard");
        capture("unsaved-reload-guard");
        click("unsaved:cancel");
        check("cancelled-reload");
        click("menu:File");
        click("file:Reload from disk");
        check("unsaved-guard");
        click("unsaved:discard");
        check("reload");
        capture("reloaded-scene");
        create("Rendering", "Camera");
        check("camera");
        text("transform:forge.position:2", "-5", true);
        // At z=-5 the default Scene viewpoint is only one metre away. Keep
        // the camera at its eye height so its marker is inside the image.
        text("transform:forge.position:1", "1", true);
        capture("camera-scene-and-inspector");
        create("Rendering", "Light");
        check("light");
        text("transform:forge.position:1", "3", true);
        capture("light-scene-and-inspector");
        click("camera-marker");
        check("picked-camera");
        capture("camera-picked-in-scene");
        click("light-marker");
        check("picked-light");
        capture("light-picked-in-scene");
        click("preview-light");
        capture("authored-scene-lighting");
        click("preview-light");
        capture("preview-lighting-restored");
        click("menu:Assets");
        capture("asset-action-menu");
        click("action:asset.cache");
        check("before-cache");
        click("cache:statistics");
        check("cache-complete");
        capture("cache-statistics");
        click("cache:verify");
        check("cache-complete");
        capture("cache-verified");
        click("cache:close");

        for (int i = 0; i < 5; ++i)
            key(ImGuiKey_Equal, true);
        check("zoom");
        capture("light-ui-150");
        key(ImGuiKey_0, true);
        click("icon:play");
        check("playing");
        capture("game-camera");
        click("tab:Problems");
        check("no-domain-errors");
        capture("runtime-problems");
        key(ImGuiKey_F6);
        check("paused");
        capture("game-paused");
        key(ImGuiKey_F7);
        check("stepped");
        capture("game-stepped");
        click("icon:stop");
        check("stopped");
        capture("returned-to-edit");
        // File-drop uses SDL's production route. The fixture supplies raw DCC
        // source files only; every import/publication/placement uses real controls.
        hover("content-results");
        steps_.push_back({Kind::DropFile, "external-glTF"});
        capture("source-import-review");
        click("button:Prepare import");
        click("button:Copy sources");
        check("source-imported");
        capture("source-import-complete");
        click("button:Close");
        text("content:search", "workflow.gltf model");
        click("source:Assets/Imported/Source-1/workflow.gltf");
        click("menu:Assets");
        click("action:asset.open");
        check("model-ready");
        capture("imported-model-preview");
        click("button:Import / Reimport");
        check("model-ready");
        click("button:Place model");
        check("model-placed");
        click("tab:Scene");
        capture("placed-imported-model");
        key(ImGuiKey_Z, true);
        check("model-undone");
        key(ImGuiKey_Y, true);
        check("model-placed");
        click("placed-model-row");
        key(ImGuiKey_S, true);
        check("saved");
        // Simulate an external DCC save, without invoking the importer directly.
        steps_.push_back({Kind::SourceEdit, "corrupt-external-model"});
        check("hot-reimport-rejected");
        click("tab:Problems");
        click("failed-model-problem");
        check("failed-asset-selected");
        capture("rejected-model-source-keeps-last-good");
        click("tab:Scene");
        steps_.push_back({Kind::SourceEdit, "change-external-model-material"});
        check("hot-reimported");
        capture("hot-reimported-model");
        for (int i = 0; i < 5; ++i)
            key(ImGuiKey_Equal, true);
        capture("imported-content-150");
        for (int i = 0; i < 5; ++i)
            key(ImGuiKey_Equal, true);
        capture("imported-content-200");
        key(ImGuiKey_0, true);
    }
    bool done() const { return index_ == steps_.size(); }
    void platform_input(SDL_WindowID window) {
        if (done())
            return;
        const auto& step = steps_[index_];
        if (step.kind == Kind::DropFile && frame_ == 0) {
            const auto it = ui_targets.find("content-results");
            if (it == ui_targets.end())
                return;
            const auto origin = ImGui::GetMainViewport()->Pos;
            const auto& target = it->second;
            for (auto type : {SDL_EVENT_DROP_BEGIN, SDL_EVENT_DROP_POSITION, SDL_EVENT_DROP_FILE,
                              SDL_EVENT_DROP_COMPLETE}) {
                SDL_Event event{};
                event.type = type;
                event.drop.windowID = window;
                event.drop.x = (target.minimum.x + target.maximum.x) * .5f - origin.x;
                event.drop.y = (target.minimum.y + target.maximum.y) * .5f - origin.y;
                event.drop.data = type == SDL_EVENT_DROP_FILE ? drop_path_.c_str() : nullptr;
                if (!SDL_PushEvent(&event))
                    throw std::runtime_error(SDL_GetError());
            }
        }
        if (step.kind == Kind::SourceEdit && frame_ == 0)
            write(project_ / "Assets/Imported/Source-1/workflow.gltf",
                  step.value == "corrupt-external-model" ? "{" : model_source(true).dump());
        // Interface zoom is handled by the production SDL event loop, before ImGui.
        if (step.kind != Kind::Key || !step.control ||
            (step.key != ImGuiKey_0 && step.key != ImGuiKey_Equal) || frame_ > 1)
            return;
        SDL_Event event{};
        event.type = frame_ == 0 ? SDL_EVENT_KEY_DOWN : SDL_EVENT_KEY_UP;
        event.key.windowID = window;
        event.key.key = step.key == ImGuiKey_0 ? SDLK_0 : SDLK_EQUALS;
        event.key.scancode = step.key == ImGuiKey_0 ? SDL_SCANCODE_0 : SDL_SCANCODE_EQUALS;
        event.key.mod = SDL_KMOD_CTRL;
        event.key.down = frame_ == 0;
        if (!SDL_PushEvent(&event))
            throw std::runtime_error(SDL_GetError());
    }
    void input() {
        auto& io = ImGui::GetIO();
        io.ConfigInputTrickleEventQueue = false;
        io.AddFocusEvent(true);
        if (done())
            return;
        if (!since_)
            since_ = SDL_GetTicks();
        const auto& step = steps_[index_];
        if (SDL_GetTicks() - since_ > 12000)
            failure_ = "Timed out at step " + std::to_string(index_) + ": " + step.value + " " +
                       last_check_;
        if ((step.kind == Kind::Click || step.kind == Kind::Hover) && frame_ == 0) {
            const auto target = step.value == "saved-cube-row" ? "entity:" + cube_
                                : step.value == "failed-model-problem"
                                    ? "problem:reimport:" + model_asset_
                                : step.value == "placed-model-row"  ? "entity:" + model_root_
                                : step.value == "camera-marker"     ? "marker:" + camera_
                                : step.value == "light-marker"      ? "marker:" + light_
                                : step.value == "saved-scene-asset" ? "asset:" + scene_
                                                                    : step.value;
            const auto it = ui_targets.find(target);
            if (it == ui_targets.end() || !it->second.enabled) {
                io.AddMousePosEvent(pointer_.x, pointer_.y);
                ui_targets.clear();
                return;
            }
            const auto& t = it->second;
            pointer_ = {(t.minimum.x + t.maximum.x) * .5f, (t.minimum.y + t.maximum.y) * .5f};
            if (pointer_.y < t.clip_minimum.y || pointer_.y > t.clip_maximum.y) {
                const float direction = pointer_.y < t.clip_minimum.y ? 3.f : -3.f;
                pointer_.x = std::clamp(pointer_.x, t.clip_minimum.x, t.clip_maximum.x);
                pointer_.y = (t.clip_minimum.y + t.clip_maximum.y) * .5f;
                io.AddMousePosEvent(pointer_.x, pointer_.y);
                io.AddMouseWheelEvent(0, direction);
                ui_targets.clear();
                return;
            }
            trace_.push_back({{"step", index_},
                              {"target", target},
                              {"rect", {t.minimum.x, t.minimum.y, t.maximum.x, t.maximum.y}},
                              {"route", "ImGui queued mouse/key input"}});
        }
        io.AddMousePosEvent(pointer_.x, pointer_.y);
        if (step.kind == Kind::Click) {
            if (frame_ == 2) {
                io.AddKeyEvent(ImGuiMod_Ctrl, step.control);
                io.AddMouseButtonEvent(0, true);
            }
            if (frame_ == 3)
                io.AddMouseButtonEvent(0, false);
            if (frame_ == 4)
                io.AddKeyEvent(ImGuiMod_Ctrl, false);
        } else if (step.kind == Kind::Text) {
            if (frame_ == 0) {
                io.AddKeyEvent(ImGuiMod_Ctrl, true);
                io.AddKeyEvent(ImGuiKey_A, true);
            }
            if (frame_ == 1) {
                io.AddKeyEvent(ImGuiKey_A, false);
                io.AddKeyEvent(ImGuiMod_Ctrl, false);
            }
            if (frame_ == 2)
                io.AddInputCharactersUTF8(step.value.c_str());
            if (frame_ == 3)
                io.AddKeyEvent(ImGuiKey_Enter, true);
            if (frame_ == 4)
                io.AddKeyEvent(ImGuiKey_Enter, false);
        } else if (step.kind == Kind::Key && frame_ < 2) {
            io.AddKeyEvent(ImGuiMod_Ctrl, frame_ == 0 && step.control);
            io.AddKeyEvent(step.key, frame_ == 0);
        }
        ++frame_;
        ui_targets.clear();
    }
    void finish(const Json& state, const std::function<void(const std::string&)>& image,
                const std::function<void(const Json&)>& record) {
        if (done())
            return;
        if (!failure_.empty()) {
            image("FAILED-" + std::to_string(index_));
            Json available = Json::object();
            for (const auto& [name, target] : ui_targets)
                available[name] = {
                    {"enabled", target.enabled},
                    {"rect",
                     {target.minimum.x, target.minimum.y, target.maximum.x, target.maximum.y}}};
            record({{"ok", false},
                    {"error", failure_},
                    {"trace", trace_},
                    {"state", state},
                    {"available_controls", available}});
            throw std::runtime_error(failure_);
        }
        if (frame_ < 8 || SDL_GetTicks() - since_ < 100)
            return;
        const auto& step = steps_[index_];
        if (step.kind == Kind::Check) {
            try {
                verify(step.value, state);
            } catch (const std::exception& e) {
                last_check_ = e.what();
                return;
            }
        }
        if (step.kind == Kind::Capture) {
            const auto name = std::to_string(index_) + "-" + step.value;
            image(name);
            trace_.push_back({{"step", index_},
                              {"image", "editor-" + name + ".ppm"},
                              {"ui_scale", state.at("ui_scale")}});
        }
        constexpr const char* names[] = {
            "click",  "hover",   "type",          "shortcut",
            "assert", "capture", "SDL file drop", "external source edit"};
        trace_.push_back({{"step", index_},
                          {"operation", names[int(step.kind)]},
                          {"value", step.value},
                          {"key", step.key == ImGuiKey_None ? "" : ImGui::GetKeyName(step.key)},
                          {"duration_ms", SDL_GetTicks() - since_},
                          {"control", step.control},
                          {"ok", true}});
        ++index_;
        frame_ = 0;
        since_ = 0;
        last_check_.clear();
        record({{"ok", done()},
                {"completed_steps", index_},
                {"total_steps", steps_.size()},
                {"trace", trace_},
                {"state", state}});
    }
};
} // namespace forge::test
