#include "animation_component_values.hpp"
#include "asset_bytes.hpp"
#include "transform_animation_preview.hpp"
#include "ui_test_renderer.hpp"
#include <forge/authoring.hpp>
#include <forge/prefab_authoring.hpp>
#include <forge/runtime.hpp>
#include <forge/runtime_ui.hpp>
#include <forge/ui_values.hpp>
#include <fstream>
#include <iostream>
#include <limits>
using namespace forge;
namespace {
void check(bool value, const char* why) {
    if (!value)
        throw std::runtime_error(why);
}
template <class F> void rejects(F&& action) {
    try {
        action();
    } catch (const std::exception&) {
        return;
    }
    throw std::runtime_error("Invalid UI Values operation was accepted");
}
void near(double actual, double expected) {
    check(std::abs(actual - expected) < 1e-5, "UI number/property differs from expected value");
}
const Json& type(const Json& schema) {
    for (const auto& item : schema.at("components"))
        if (item.at("id") == "forge.ui_values")
            return item;
    throw std::runtime_error("UI Values schema missing");
}
void names() {
    for (const auto* name : {"amount", "A1_b", "forge_locale", "forge_state_menu"})
        check(UiValues::valid_name(name), "Valid model name rejected");
    for (const auto* name :
         {"", "_name", "1name", "a.b", "a-b", "a b", "ev", "FALSE", "It", "it_index", "literal",
          "size", "True", "tick", "paused", "forge_loading_x"})
        check(!UiValues::valid_name(name), "Reserved/invalid native model name accepted");
    check(UiValues::valid_name(std::string(64, 'a')) && !UiValues::valid_name(std::string(65, 'a')),
          "UI name byte bound differs");
    UiValues maximum;
    for (std::size_t i = 0; i < UiValues::max_entries; ++i)
        maximum.values.push_back({"item" + std::to_string(i), double(i)});
    maximum.validate();
    maximum.values.push_back({"excess", 0});
    rejects([&] { maximum.validate(); });
    for (const double value :
         {std::numeric_limits<double>::infinity(), std::numeric_limits<double>::quiet_NaN()})
        rejects([&] { UiValues{{{"amount", value}}}.validate(); });
    rejects([] { UiValues{{{"amount", 1}, {"amount", 2}}}.validate(); });
}
void authored_and_tracks() {
    WorldContext world;
    Scene scene(world);
    scene.replace(
        {{"version", 1},
         {"entities",
          Json::array({{{"id", "hud"}, {"name", "HUD"}, {"components", Json::object()}}})}});
    const auto before = scene.document();
    auto edited = before;
    edited["entities"][0]["components"]["forge.ui_values"] = {
        {"values", Json::array({{{"name", "amount"}, {"value", .25}, {"future", "retained"}},
                                {{"name", "other"}, {"value", 7}}})},
        {"extension", true}};
    scene.edit(edited);
    check(scene.document() == edited, "UI Values lost unknown authored extensions");
    scene.undo();
    check(scene.document() == before, "UI Values Undo changed unrelated content");
    scene.redo();
    check(scene.document() == edited, "UI Values Redo differs");
    for (const auto& invalid : Json::array({true, "bad", nullptr})) {
        auto bad = edited;
        bad["entities"][0]["components"]["forge.ui_values"]["values"][0]["value"] = invalid;
        rejects([&] { scene.edit(bad); });
        check(scene.document() == edited, "Rejected value mutated source");
    }
    const auto schema = scene.schema();
    const auto& declaration = type(schema);
    const auto current = scene.effective_document()["entities"][0]["components"]["forge.ui_values"];
    check(declaration.at("optional") && declaration.at("category") == "Runtime UI" &&
              declaration.at("fields")[0].at("element_key") == "name" &&
              declaration.at("fields")[0].at("maximum_count") == UiValues::max_entries,
          "Discoverable keyed UI metadata differs");
    const auto fields = animation_component_fields(declaration, &current);
    check(fields.size() == 2 &&
              fields[0].element_path == std::vector<Json>({{{"key", "amount"}}, "value"}),
          "Timeline did not discover named numeric leaves");
    auto source = TransformAnimationSource::create(AssetId::generate());
    source.add_component_field(fields[0], .25);
    source.document["channels"] = Json::array();
    const auto last = source.set_key(animation_timebase, {});
    source.set_component_value(last, fields[0], .75);
    const auto decoded = decode_transform_animation(source.cook(), source.asset());
    check(decoded.component_bindings == source.clip().component_bindings,
          "Builtin admission lost in cooked clip");
    const auto sampled =
        sample_animation_component(declaration, current, decoded, animation_timebase / 2, true);
    near(sampled["values"][0]["value"], .5);
    auto projected = Json{{"forge.ui_values", current}};
    auto sampled_known = sampled;
    sampled_known["values"][0].erase("future");
    merge_animation_properties(projected,
                               {{"forge.ui_values", {{"values", sampled_known["values"]}}}}, world);
    check(projected["forge.ui_values"]["values"][0]["future"] == "retained",
          "Animated builtin projection lost opaque keyed-entry extensions");
    check(sampled["values"][0]["future"] == "retained" && sampled["extension"] == true,
          "Sampled UI value lost unknown extensions");
    auto reordered = current;
    std::swap(reordered["values"][0], reordered["values"][1]);
    near(sample_animation_component(declaration, reordered, decoded, animation_timebase / 2,
                                    true)["values"][1]["value"],
         .5);
    auto missing = current;
    missing["values"][0]["name"] = "renamed";
    rejects([&] { sample_animation_component(declaration, missing, decoded, 0, true); });
    auto mismatched = declaration;
    mismatched["admission"]["digest"] = std::string(64, '0');
    rejects([&] { sample_animation_component(mismatched, current, decoded, 0, true); });
    auto preview = scene.effective_document();
    const auto authored = scene.document();
    check(apply_transform_animation_preview(preview, scene.reference("hud"), decoded,
                                            animation_timebase / 2, &schema),
          "Copied Timeline preview missed UI Values");
    near(preview["entities"][0]["components"]["forge.ui_values"]["values"][0]["value"], .5);
    check(scene.document() == authored, "UI preview mutated Scene/history");
    EngineContext runtime(WorldRole::Runtime);
    Scene live(runtime.world());
    live.restore_snapshot(authored);
    auto entity = live.entity(scene.reference("hud").entity.str());
    const auto candidates = prepare_animation_components(runtime.world(), entity, decoded,
                                                         animation_timebase / 2, true);
    near(entity.get<UiValues>().values[0].value, .25);
    for (const auto& candidate : candidates)
        candidate.apply(entity);
    near(entity.get<UiValues>().values[0].value, .5);
    near(entity.get<UiValues>().values[1].value, 7);
    const auto tuned =
        live.tune_runtime_property(scene.reference("hud").entity.str(), "forge.ui_values", "values",
                                   Json::array({{{"name", "amount"}, {"value", .9}}}));
    near(tuned[0]["value"], .9);
    rejects([&] {
        live.tune_runtime_property(scene.reference("hud").entity.str(), "forge.ui_values", "values",
                                   Json::array({{{"name", "amount"}, {"value", false}}}));
    });
    near(entity.get<UiValues>().values[0].value, .9);
}
void prefab_values(const std::filesystem::path& root) {
    std::filesystem::create_directories(root);
    EngineContext engine;
    Scene scene(engine.world());
    const auto source = authoring_command(scene, "entity.create", {{"name", "UI source"}})
                            .at("selected")
                            .get<std::string>();
    authoring_command(scene, "component.add",
                      {{"entity", source}, {"component", "forge.ui_values"}});
    const auto original =
        Json::array({{{"name", "amount"}, {"value", .25}, {"future", "retained"}}});
    authoring_command(scene, "property.set",
                      {{"entity", source},
                       {"component", "forge.ui_values"},
                       {"field", "values"},
                       {"value", original}});
    PrefabLibrary library(root);
    const auto prefab =
        library.create(scene, create_prefab_source(scene, source), "UI.prefab.json");
    const auto instance = instantiate_prefab(scene, prefab);
    check(!scene.entity(instance).owns<UiValues>(), "UI Values did not inherit its source");
    near(scene.entity(instance).get<UiValues>().values[0].value, .25);
    auto override = original;
    override[0]["value"] = .75;
    authoring_command(scene, "property.set",
                      {{"entity", instance},
                       {"component", "forge.ui_values"},
                       {"field", "values"},
                       {"value", override}});
    auto before = library.source(prefab), next = before;
    next["members"][0]["components"]["forge.ui_values"]["values"][0]["value"] = .5;
    library.publish(scene, before, next);
    near(scene.entity(instance).get<UiValues>().values[0].value, .75);
    authoring_command(
        scene, "property.revert",
        {{"entity", instance}, {"component", "forge.ui_values"}, {"field", "values"}});
    near(scene.entity(instance).get<UiValues>().values[0].value, .5);
    check(scene.undo() && scene.entity(instance).get<UiValues>().values[0].value == .75 &&
              scene.redo(),
          "UI Values Revert bypassed history");
    bool projected = false;
    const auto rows = scene.ui_document_rows();
    for (const auto& row : rows)
        if (row.at("id") == instance) {
            projected = true;
            near(row.at("components").at("forge.ui_values").at("values")[0]["value"], .5);
        }
    check(projected, "UI projection omitted inherited values");
    EngineContext reopened;
    Scene loaded(reopened.world());
    PrefabLibrary fresh(root);
    fresh.load_scene(loaded, scene.document());
    near(loaded.entity(instance).get<UiValues>().values[0].value, .5);
}
void copied_model(const std::filesystem::path& root, const std::vector<std::byte>& font) {
    std::filesystem::create_directories(root);
    std::ofstream(root / "hud.rml")
        << R"(<rml><head><style>body {font-family:Lato;} #bound {width:100px;height:50px;} .hot {color:#ff0000;}</style></head><body><p id="bound" data-style-opacity="amount" data-class-hot="amount > 0.5">{{amount}}</p></body></rml>)";
    const auto asset = register_ui_document(root, "hud.rml");
    EngineContext engine(WorldRole::Runtime, false, {ui_module(root)});
    Scene scene(engine.world());
    scene.restore_snapshot(
        {{"version", 1},
         {"entities",
          Json::array({{{"id", "hud"}, {"name", "HUD"}, {"components", Json::object()}}})}});
    auto entity = scene.entity("hud");
    entity.set<UiDocument>({{asset.id}, true, true, 0});
    entity.set<UiValues>({{{"amount", .25}}});
    const auto id = scene.reference("hud").entity;
    auto ui = std::static_pointer_cast<UiRuntime>(engine.services().ui());
    const auto snapshot = [&] { return ui->snapshot(scene, "ui-values", 1, 42, true); };
    const auto first = snapshot();
    near(first["documents"][0]["model"]["amount"], .25);
    UiTestRenderer renderer;
    {
        UiPresenter presenter(renderer, root, font);
        presenter.reset("ui-values", 1);
        check(presenter.accept(first), presenter.diagnostic().c_str());
        presenter.update(0, 640, 480);
        presenter.render();
        auto* document = Rml::GetContext(0)->GetDocument(0);
        auto* bound = document->GetElementById("bound");
        near(bound->GetProperty<float>("opacity"), .25);
        check(!bound->IsClassSet("hot"), "Initial bound class differs");
        const auto revision = presenter.presentation_revision();
        entity.set<UiValues>({{{"amount", .75}}});
        check(presenter.accept(snapshot()), "Updated copied UI model rejected");
        presenter.update(.05, 640, 480);
        presenter.render();
        near(bound->GetProperty<float>("opacity"), .75);
        check(bound->IsClassSet("hot") && Rml::GetContext(0)->GetDocument(0) == document &&
                  presenter.presentation_revision() == revision,
              "Numeric UI update replaced document or failed native class expression");
        ui->publish(id, "cpp_only", 3);
        near(snapshot()["documents"][0]["model"]["cpp_only"], 3);
        ui->publish(id, "amount", 9);
        rejects(snapshot);
        entity.remove<UiValues>();
        near(snapshot()["documents"][0]["model"]["amount"], 9);
        UiValues values;
        for (std::size_t i = 0; i < UiValues::max_entries; ++i)
            values.values.push_back({"named" + std::to_string(i), double(i)});
        entity.set(values);
        for (int i = 0; i < 12; ++i)
            ui->publish(id, "cpp" + std::to_string(i), i);
        check(snapshot()["documents"][0]["model"].size() == ui_protocol::max_values,
              "The shared model bound rejected its exact maximum");
        ui->publish(id, "cpp12", 12);
        rejects(snapshot); // 16 authored + 15 C++ + tick/paused exceeds the shared32 limit.
    }
    check(renderer.geometry.empty() && renderer.textures.empty(), "UI resources did not retire");
}
} // namespace
int main(int argc, char** argv) {
    try {
        check(argc == 3, "Need scratch root and font");
        names();
        authored_and_tracks();
        prefab_values(std::filesystem::path(argv[1]) / AssetId::generate().str());
        copied_model(std::filesystem::path(argv[1]) / AssetId::generate().str(),
                     asset_detail::read_bytes(argv[2], 4 * 1024 * 1024));
        std::cout << "UI Values names/bounds, history, keyed component tracks, copied preview, "
                     "native playback, live tuning and DOM bindings passed\n";
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
