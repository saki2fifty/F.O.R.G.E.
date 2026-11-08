#pragma once
#include "imgui_software_capture.hpp"
#include "prefabs.hpp"

namespace forge::test {
inline void exercise_prefab_source_layers(const std::filesystem::path& scratch, float scale,
                                          bool narrow, const std::filesystem::path& image = {}) {
    const auto check = [](bool value, const std::string& message) {
        if (!value)
            throw std::runtime_error(message);
    };
    const auto root = scratch / AssetId::generate().str();
    std::filesystem::create_directories(scratch);
    SceneDocument::create_project(root, "Source contributions");
    EngineContext engine;
    Scene scene(engine.world());
    SceneDocument project(scene);
    project.open_project(root);
    const auto original =
        authoring_command(scene, "entity.create", {{"kind", 0}}).at("selected").get<std::string>();
    const auto base = project.prefabs().create(scene, create_prefab_source(scene, original),
                                               "Assets/Common.prefab.json");
    const auto a = project.prefabs().create_variant(scene, base, "Assets/Shape.prefab.json");
    const auto b = project.prefabs().create_variant(scene, base, "Assets/Green.prefab.json");
    const auto c = project.prefabs().create_variant(scene, base, "Assets/Darker.prefab.json");
    for (const auto asset : {a, b, c}) {
        const auto before = project.prefabs().source(asset);
        auto candidate = before;
        candidate["overrides"].push_back(
            {{"id", before.at("root")},
             {"components", Json::object()},
             {"property_overrides",
              {{"forge.tint", asset == a ? Json{{"r", .2}} : Json{{"g", asset == b ? .8 : .6}}}}}});
        project.prefabs().publish(scene, before, candidate,
                                  project.prefabs().base_sources(scene, before, &candidate));
    }
    const auto combined = project.prefabs().create_variant(scene, a, "Assets/Combined.prefab.json");
    const auto source_a = project.prefabs().source(a), source_b = project.prefabs().source(b);
    ImGuiSoftwareCapture capture(int((narrow ? 820 : 1180) * scale), int(1080 * scale));
    ui::style(scale);
    ui::EditorUiContext context;
    ui::ContextScope context_scope(context);
    context.scene = &scene;
    PrefabEditor editor;
    editor.edit_source(scene, project, combined);
    bool locked = false;
    observe_ui = true;
    const auto frame = [&] {
        ui_targets.clear();
        capture.new_frame();
        ImGui::SetNextWindowPos({10 * scale, 10 * scale});
        ImGui::SetNextWindowSize({(narrow ? 800 : 1160) * scale, 1060 * scale});
        editor.draw(scene, project, locked);
        capture.render();
    };
    const auto click = [&](const std::string& id) {
        frame();
        check(ui_targets.contains(id), "Missing source-layer control: " + id);
        auto box = ui_targets.at(id);
        const auto center = [](const auto& bounds) {
            return ImVec2{(bounds.minimum.x + bounds.maximum.x) / 2,
                          (bounds.minimum.y + bounds.maximum.y) / 2};
        };
        auto point = center(box);
        if (point.y < box.clip_minimum.y || point.y > box.clip_maximum.y) {
            auto* window = ImGui::FindWindowByName("###Prefab source");
            check(window, "Missing source-layer window");
            ImGui::SetScrollY(window, window->Scroll.y + point.y -
                                          (box.clip_minimum.y + box.clip_maximum.y) / 2);
            frame();
            frame();
            box = ui_targets.at(id);
            point = center(box);
        }
        check(box.enabled && point.x >= box.clip_minimum.x && point.x <= box.clip_maximum.x &&
                  point.y >= box.clip_minimum.y && point.y <= box.clip_maximum.y,
              "Clipped or disabled source-layer control: " + id);
        auto& io = ImGui::GetIO();
        io.AddMousePosEvent(point.x, point.y);
        frame();
        io.AddMouseButtonEvent(0, true);
        frame();
        io.AddMouseButtonEvent(0, false);
        frame();
        frame();
    };
    const auto green = [&] {
        const auto view = editor.source_view(scene.schema());
        for (const auto& member : view.at("members"))
            if (member.at("id") == view.at("root"))
                return member.at("components").at("forge.tint").at("g").get<double>();
        throw std::runtime_error("Source-layer editor lost its root");
    };
    frame();
    frame();
    click("prefab-mixins-section");
    click("prefab-mixin-add");
    click("prefab-mixin-choice:" + b.str());
    check(editor.migration_document().at("version") == 8 && std::abs(green() - .8) < 1e-6,
          "Native add did not enable source8 or compose the selected sparse contribution");
    click("prefab-mixin-add");
    click("prefab-mixin-choice:" + c.str());
    check(std::abs(green() - .6) < 1e-6, "Native later source did not take precedence");
    click("prefab-mixin-earlier:" + c.str());
    check(std::abs(green() - .8) < 1e-6, "Native Earlier did not reorder source precedence");
    click("prefab-mixin-later:" + c.str());
    check(std::abs(green() - .6) < 1e-6, "Native Later did not reorder source precedence");
    click("prefab-mixin-remove:" + c.str());
    check(std::abs(green() - .8) < 1e-6, "Native Remove did not follow the remaining contributor");
    locked = true;
    frame();
    check(!ui_targets.at("prefab-mixin-add").enabled &&
              !ui_targets.at("prefab-mixin-remove:" + b.str()).enabled,
          "Source contribution controls ignored the existing gameplay lock");
    locked = false;
    click("button:Publish source");
    check(!editor.dirty() && project.prefabs().source(combined).at("mixins") == Json::array({b}) &&
              project.prefabs().source(a) == source_a && project.prefabs().source(b) == source_b,
          "Native publication changed a supporting source or lost ordered raw intent");
    const auto& edges = project.prefabs().records().at(combined).dependency_edges;
    check(std::any_of(edges.begin(), edges.end(),
                      [&](const auto& edge) {
                          return edge.target == b && edge.role == "document:mixins.0";
                      }),
          "Published dependency evidence omitted the additional source input");
    const auto instance = instantiate_prefab(scene, combined);
    check(std::abs(scene.entity(instance).get<Tint>().r - .2f) < 1e-6f &&
              std::abs(scene.entity(instance).get<Tint>().g - .8f) < 1e-6f,
          "Published native instance did not consume the composed source");
    if (!image.empty()) {
        ImGui::SetScrollY(ImGui::FindWindowByName("###Prefab source"), 0);
        ImGui::GetIO().AddMousePosEvent(-FLT_MAX, -FLT_MAX);
        frame();
        frame();
        std::filesystem::create_directories(image.parent_path());
        capture.save(image);
    }
    observe_ui = false;
}
} // namespace forge::test
