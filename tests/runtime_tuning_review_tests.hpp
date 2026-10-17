#pragma once
#include "../src/authored_schema.hpp"
#include "runtime_tuning_review.hpp"
#include <forge/prefab_authoring.hpp>
void require(bool condition, const char* message);
namespace forge::test {
struct ReviewValues {
    float speed;
    std::uint64_t counter;
    std::string notes;
};
struct ReviewManyFields {
    float values[129];
};
inline void test_runtime_tuning_review() {
    Json declarations;
    {
        EngineContext registration(WorldRole::Validation);
        auto& world = registration.world().world();
        const auto type = world.component<ReviewValues>()
                              .member<float>("speed")
                              .member<std::uint64_t>("counter")
                              .member<std::string>("notes");
        detail::opt_in_authoring(world, type, "project.review", "project.reviewgame", 1,
                                 {{"speed", 90.f}, {"counter", UINT64_MAX}, {"notes", "Initial"}},
                                 "Gameplay");
        auto many = world.component<ReviewManyFields>();
        Json defaults = Json::object();
        for (unsigned index = 0; index < 129; ++index) {
            const auto name = "value" + std::to_string(index);
            many.member<float>(name.c_str(), 0, index * sizeof(float));
            defaults[name] = 0.f;
        }
        detail::opt_in_authoring(world, many, "project.review_many", "project.reviewgame", 1,
                                 defaults, "Gameplay");
        declarations = detail::export_authored_types(world);
    }
    const auto root =
        std::filesystem::current_path() / ("tuning-review-" + AssetId::generate().str());
    SceneDocument::create_project(root, "Runtime tuning review");
    struct Cleanup {
        std::filesystem::path path;
        ~Cleanup() {
            std::error_code error;
            std::filesystem::remove_all(path, error);
        }
    } cleanup{root};
    EngineContext engine;
    Scene scene(engine.world());
    SceneDocument document(scene);
    document.open_project(root);
    scene.publish_component_schemas(declarations);
    const std::string id =
        authoring_command(scene, "entity.create", {{"name", "Tuned cube"}}).at("selected");
    authoring_command(scene, "component.add", {{"entity", id}, {"component", "project.review"}});
    auto baseline = scene.document();
    baseline["entities"][0]["components"]["project.review"]["plugin"] = {{"opaque", true}};
    scene.reset(baseline);
    baseline = scene.document();
    const auto schema = scene.schema();
    auto runtime = scene.effective_document();
    auto& value = runtime["entities"][0]["components"]["project.review"];
    value["speed"] = 180.f;
    value["counter"] = UINT64_MAX - std::uint64_t{1};
    value["notes"] = "Runtime text";
    runtime["entities"][0]["components"]["forge.local_translation"]["x"] = 999;
    runtime["entities"][0]["components"]["private.runtime"] = {{"pointer", 17}};
    RuntimeTuningReview review;
    const auto capture = [&] { return review.capture(scene, document, runtime, schema, id, 17); };
    require(capture() && review.pending() && review.fields().size() == 3 &&
                scene.document() == baseline,
            "Review did not freeze only changed admitted fields without authoring");
    runtime["entities"][0]["components"]["project.review"]["speed"] = 360.f;
    require(!capture() && review.fields().size() == 3 &&
                std::any_of(review.fields().begin(), review.fields().end(),
                            [](const auto& field) {
                                return field.property == "speed" && field.after == 180.f;
                            }),
            "A second capture silently replaced the existing frozen review");
    for (std::size_t index = 0; index < review.fields().size(); ++index)
        if (review.fields()[index].property == "notes")
            review.select(index, false);
    require(!review.apply(scene, document, true) && review.pending() &&
                scene.document() == baseline,
            "Review applied during Play or lost the pending values");
    require(review.apply(scene, document, false) && !review.pending(),
            "Selected runtime values failed ordinary authoring validation");
    const auto applied = scene.document();
    const auto& result = applied["entities"][0]["components"]["project.review"];
    require(
        result["speed"] == 180.f && result["counter"].get<std::uint64_t>() == UINT64_MAX - 1 &&
            result["notes"] == "Initial" && result["plugin"] == Json{{"opaque", true}} &&
            result["$forge"] == baseline["entities"][0]["components"]["project.review"]["$forge"] &&
            !applied["entities"][0]["components"].contains("private.runtime") &&
            applied["entities"][0]["components"]["forge.local_translation"]["x"] == 0,
        "Review rounded an integer, copied a pose/private field, or lost unselected/opaque data");
    require(scene.undo() && scene.document() == baseline && !scene.undo(),
            "Runtime review did not produce exactly one Scene Undo");
    require(scene.redo() && scene.document() == applied, "Runtime review Redo lost values");
    document.save();
    document.open_scene(document.path());
    require(scene.document() == applied,
            "Reviewed values did not survive ordinary Scene Save/reopen");

    scene.reset(baseline);
    require(capture(), "Could not capture a fresh review");
    scene.rename_entity(id, "Authored change");
    const auto changed = scene.document();
    require(!review.apply(scene, document, false) && review.pending() &&
                scene.document() == changed,
            "Stale review overwrote an unrelated authored edit");
    review.discard();
    scene.reset(baseline);
    require(capture(), "Could not capture before reopening");
    document.save();
    document.open_scene(document.path());
    require(!review.apply(scene, document, false) && review.pending() &&
                scene.document() == baseline,
            "Review crossed a SceneDocument generation after reopening");
    review.discard();

    auto readonly_schema = schema;
    for (auto& type : readonly_schema["components"])
        if (type["id"] == "project.review")
            for (auto& field : type["fields"])
                if (field["id"] == "notes")
                    field["read_only"] = true;
    require(review.capture(scene, document, runtime, readonly_schema, id, 19) &&
                review.fields().size() == 2,
            "Runtime read-only metadata was ignored during review capture");
    for (std::size_t index = 0; index < review.fields().size(); ++index)
        review.select(index, false);
    require(!review.apply(scene, document, false) && review.pending() &&
                scene.document() == baseline,
            "Empty selection applied values or discarded the review");
    review.discard();

    auto incompatible = runtime;
    incompatible["entities"][0]["components"]["project.review"]["$forge"]["schema_version"] = 99;
    require(!review.capture(scene, document, incompatible, schema, id, 20) && !review.pending(),
            "Incompatible runtime component admission was copied");
    auto wrong_scene = runtime;
    wrong_scene["asset_id"] = AssetId::generate();
    require(!review.capture(scene, document, wrong_scene, schema, id, 20),
            "Review accepted a different runtime Scene identity");
    auto generated = runtime;
    generated["entities"][0]["id"] = EntityId::generate().str();
    require(!review.capture(scene, document, generated, schema, generated["entities"][0]["id"], 20),
            "Runtime-only entity was accepted as an authored target");
    require(!review.capture(scene, document, scene.effective_document(), schema, id, 20),
            "No-op runtime snapshot produced a review");
    auto excessive = runtime;
    excessive["entities"][0]["components"]["project.review"]["notes"] =
        std::string(1024 * 1024 + 1, 'x');
    require(!review.capture(scene, document, excessive, schema, id, 20) && !review.pending() &&
                scene.document() == baseline,
            "Oversized review retained a partial candidate or changed the Scene");
    auto invalid = runtime;
    invalid["entities"][0]["components"]["project.review"]["counter"] = -1;
    require(review.capture(scene, document, invalid, schema, id, 20) &&
                !review.apply(scene, document, false) && review.pending() &&
                scene.document() == baseline,
            "Invalid selected subset partially committed or lost its review");
    review.discard();

    const auto definition = create_prefab_source(scene, id);
    const auto asset = document.prefabs().create(scene, definition, "Assets/Review.prefab.json");
    const auto instance = instantiate_prefab(scene, asset);
    auto prefab_runtime = scene.effective_document();
    for (auto& row : prefab_runtime["entities"])
        if (row["id"] == instance)
            row["components"]["project.review"]["speed"] = 270.f;
    const auto prefab_before = scene.document();
    require(review.capture(scene, document, prefab_runtime, schema, instance, 30) &&
                review.fields().size() == 1 && review.apply(scene, document, false),
            "Prefab instance runtime tuning could not be reviewed/applied");
    const auto prefab_after = scene.document();
    for (const auto& row : prefab_after["entities"])
        if (row["id"] == instance)
            require(!row["components"].contains("project.review") &&
                        row["property_overrides"]["project.review"]["speed"] == 270.f &&
                        !row["property_overrides"]["project.review"].contains("counter"),
                    "Review created a full prefab component override or copied unselected fields");
    require(scene.undo() && scene.document() == prefab_before && scene.redo() &&
                scene.document() == prefab_after,
            "Runtime review did not preserve independent Prefab override intent through history");

    scene.reset(baseline);
    auto source_readonly = declarations;
    for (auto& type : source_readonly)
        if (type["id"] == "project.review")
            for (auto& field : type["structure"]["fields"])
                if (field["id"] == "notes")
                    field["read_only"] = true;
    const auto admitted_schema = scene.schema();
    bool projection_rejected = false;
    try {
        scene.publish_component_schemas(source_readonly);
    } catch (const std::exception&) {
        projection_rejected = true;
    }
    require(projection_rejected && scene.schema() == admitted_schema &&
                scene.document() == baseline,
            "Unsupported native projection replaced admitted metadata or authored values");
    require(capture(), "Could not capture before metadata refresh");
    auto changed_defaults = declarations;
    for (auto& type : changed_defaults)
        if (type["id"] == "project.review")
            type["defaults"]["speed"] = 91.f;
    scene.publish_component_schemas(changed_defaults);
    require(!review.apply(scene, document, false) && review.pending() &&
                scene.document() == baseline,
            "Review crossed a changed schema generation or discarded its candidate");
    review.discard();
    scene.publish_component_schemas(declarations);
    require(capture(), "Could not capture before project switch");
    SceneDocument::create_project(root / "OtherProject", "Other project");
    document.open_project(root / "OtherProject");
    const auto other = scene.document();
    require(!review.apply(scene, document, false) && review.pending() && scene.document() == other,
            "Review wrote into another project or lost its pending values");
    review.discard();
    document.open_project(root);
    scene.reset(baseline);
    authoring_command(scene, "component.add",
                      {{"entity", id}, {"component", "project.review_many"}});
    const auto many_before = scene.document();
    auto many_runtime = scene.effective_document();
    auto& many_values = many_runtime["entities"][0]["components"]["project.review_many"];
    for (unsigned index = 0; index < 128; ++index)
        many_values["value" + std::to_string(index)] = 1.f;
    require(review.capture(scene, document, many_runtime, schema, id, 50) &&
                review.fields().size() == 128 && review.apply(scene, document, false) &&
                scene.undo() && scene.document() == many_before,
            "The supported 128-field review failed atomic publication/history");
    many_values["value128"] = 1.f;
    require(!review.capture(scene, document, many_runtime, schema, id, 51) && !review.pending() &&
                scene.document() == many_before,
            "A 129-field review retained partial state or changed the Scene");

    for (float scale : {1.f, 1.5f, 2.f}) {
        scene.reset(baseline);
        auto live = scene.effective_document();
        live["entities"][0]["components"]["project.review"]["speed"] = 360.f;
        live["entities"][0]["components"]["project.review"]["counter"] = UINT64_MAX - 2;
        const std::string long_value = std::string(240, 'x') + " Résumé";
        live["entities"][0]["components"]["project.review"]["notes"] = long_value;
        ImGui::CreateContext();
        ui::style(scale);
        auto& io = ImGui::GetIO();
        io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
        io.IniFilename = nullptr;
        io.DisplaySize = scale == 2 ? ImVec2{960, 640} : ImVec2{1280, 900};
        io.DeltaTime = 1.f / 60;
        io.ConfigInputTrickleEventQueue = false;
        unsigned char* pixels;
        int width, height;
        io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);
        ui::EditorUiContext context;
        ui::ContextScope context_scope(context);
        context.task.owner = ui::DocumentTask::Settings;
        ComponentInspector inspector;
        RuntimeTuningReview dialog;
        bool locked = true;
        observe_ui = true;
        const auto frame = [&] {
            ui_targets.clear();
            ImGui::NewFrame();
            ImGui::SetNextWindowPos({0, 0}, ImGuiCond_Always);
            ImGui::SetNextWindowSize(io.DisplaySize, ImGuiCond_Always);
            ImGui::Begin("Live Inspector fixture");
            inspector.draw_runtime(
                live, schema, root, id, [](const auto&, const auto&, const auto&) { return false; },
                false, [&] { dialog.capture(scene, document, live, schema, id, 70); });
            ImGui::End();
            dialog.draw(scene, document, locked);
            ImGui::Render();
        };
        const auto click = [&](const std::string& name) {
            for (unsigned attempt = 0; attempt < 12; ++attempt) {
                frame();
                require(ui_targets.contains(name), "Runtime review control is missing");
                const auto target = ui_targets.at(name);
                const ImVec2 point{(target.minimum.x + target.maximum.x) / 2,
                                   (target.minimum.y + target.maximum.y) / 2};
                require(target.enabled, "Runtime review control is disabled");
                if (point.y > target.clip_maximum.y || point.y < target.clip_minimum.y) {
                    io.AddMousePosEvent(target.clip_minimum.x + 10, target.clip_maximum.y - 10);
                    io.AddMouseWheelEvent(0, point.y > target.clip_maximum.y ? -.5f : .5f);
                    frame();
                    continue;
                }
                require(point.x >= target.clip_minimum.x && point.x <= target.clip_maximum.x,
                        "Runtime review control is horizontally clipped");
                io.AddMousePosEvent(point.x, point.y);
                io.AddMouseButtonEvent(0, true);
                frame();
                io.AddMouseButtonEvent(0, false);
                frame();
                return;
            }
            require(false, "Runtime review control could not be reached by scrolling");
        };
        frame();
        frame();
        click("runtime:keep_values");
        require(dialog.pending() && !ui_targets.at("runtime_review:apply").enabled &&
                    scene.document() == baseline,
                "Live capture wrote the Scene or enabled Apply before Stop");
        click("runtime_review:close");
        require(!dialog.open && dialog.pending(),
                "Closing the review discarded its pending values");
        click("runtime:keep_values");
        require(dialog.open && dialog.pending(), "Existing review could not be reopened");
        click("runtime_review:details:project.review:notes");
        io.AddKeyEvent(ImGuiKey_Escape, true);
        frame();
        io.AddKeyEvent(ImGuiKey_Escape, false);
        frame();
        require(!ImGui::IsPopupOpen(nullptr,
                                    ImGuiPopupFlags_AnyPopupId | ImGuiPopupFlags_AnyPopupLevel),
                "Value details did not close with Escape");
        locked = false; // The host's existing Stop/operation gate.
        frame();
        click("runtime_review:field:project.review:counter");
        require(std::any_of(dialog.fields().begin(), dialog.fields().end(),
                            [](const auto& field) {
                                return field.property == "counter" && !field.selected;
                            }),
                "Actual review checkbox did not exclude Counter");
        click("runtime_review:apply");
        require(!dialog.pending() && context.task.owner == ui::DocumentTask::Scene,
                "Apply did not clear the review or route Save/history to the Scene owner");
        const auto value = scene.document()["entities"][0]["components"]["project.review"];
        if (value["speed"] != 360.f || value["counter"].get<std::uint64_t>() != UINT64_MAX ||
            value["notes"] != long_value)
            throw std::runtime_error(
                "Actual review publication differs: " +
                Json{{"scale", scale}, {"value", value}, {"expected_notes", long_value}}.dump());
        require(scene.undo() && scene.document() == baseline && !scene.undo(),
                "Actual review Apply did not create exactly one Undo");
        click("runtime:keep_values");
        click("runtime_review:discard");
        require(!dialog.pending() && scene.document() == baseline,
                "Actual Discard changed authored values");
        observe_ui = false;
        ImGui::DestroyContext();
    }
}
} // namespace forge::test
