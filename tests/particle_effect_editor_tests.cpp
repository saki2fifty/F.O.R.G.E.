#include "asset_bytes.hpp"
#include "editor/particle_effect_editor.hpp"
#include <forge/material_graph.hpp>
#include <fstream>
#include <imgui_internal.h>
#include <iostream>
using namespace forge;
namespace {
void require(bool value, const std::string& message) {
    if (!value)
        throw std::runtime_error(message);
}
void exercise(const std::filesystem::path& root, float scale, bool compact) {
    std::filesystem::create_directories(root / "Assets");
    EngineContext engine;
    Scene scene(engine.world());
    SceneDocument project(scene);
    project.open_project(root, true);
    const std::filesystem::path source = "Assets/Original.particles.json";
    const bool legacy = scale == 1 && !compact;
    auto created = ParticleEffectDocument::create(project.writer_guard(), source, [&](AssetId id) {
        auto result = ParticleEffectSource::create(id);
        if (legacy) {
            auto values = result.compiled();
            values.version = 1;
            result.document = encode_particle_effect(values);
            result.document["asset_id"] = id;
        }
        return result;
    });
    const auto original = created->source().document;
    created.reset();
    const auto disk = asset_detail::read_bytes(root / source, particle_effect_byte_limit);
    ParticleEffectEditor editor;
    editor.open(project, source);
    const auto graph_shader = MaterialGraphSource::create_particle(AssetId::generate());
    const std::filesystem::path graph_path = "Assets/Program.shader.json";
    {
        std::ofstream out(root / graph_path);
        out << graph_shader.document.dump();
    }
    auto catalog = AssetCatalog::open_project(root);
    AssetRecord graph_record{graph_shader.asset(), ShaderAsset::type, graph_path, 1};
    // Picker projection only; this fixture does not claim a cooked/device program.
    graph_record.metadata["forge.shader"] = {{"version", 1}, {"particle_graph_profile", 1}};
    catalog.add(graph_record);
    editor.catalog_changed(std::make_shared<const AssetCatalog>(std::move(catalog)));
    require(editor.document() && editor.dirty(), "Unpublished effect failed to open");
    const auto actor =
        authoring_command(scene, "entity.create",
                          {{"name", "Particle UI fixture"}, {"recipe", "render.particles"}})
            .at("selected")
            .get<std::string>();
    const auto initial_scene = scene.document();
    ImGui::CreateContext();
    auto& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.DisplaySize = compact ? ImVec2{780, 820} : ImVec2{1500, 1800};
    io.DeltaTime = 1.f / 60;
    io.ConfigInputTrickleEventQueue = false;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    unsigned char* pixels;
    int width, height;
    io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);
    ui::style(scale);
    test::observe_ui = true;
    ui::EditorUiContext context;
    ui::ContextScope scope(context);
    context.selection.select_entity(actor);
    unsigned previews = 0, releases = 0;
    editor.draw_preview = [&](const auto& value, double time, const auto& catalog) {
        require(value.asset() == editor.document()->source().asset() && catalog &&
                    std::isfinite(time),
                "Particle preview received a different document/selection");
        require(scene.document() == initial_scene,
                "Isolated effect preview mutated the authored Scene");
        ++previews;
    };
    editor.release_preview = [&] { ++releases; };
    std::string message;
    bool locked = false;
    auto frame = [&] {
        test::ui_targets.clear();
        editor.poll(project, message);
        ImGui::NewFrame();
        ImGui::SetNextWindowPos({0, 0}, ImGuiCond_Always);
        ImGui::SetNextWindowSize({io.DisplaySize.x - 20, io.DisplaySize.y - 20}, ImGuiCond_Always);
        editor.draw(project, scene, locked);
        ImGui::Render();
    };
    frame();
    frame();
    auto point = [&](const std::string& id) {
        for (unsigned attempt = 0; attempt < 80; ++attempt) {
            require(test::ui_targets.contains(id), "Particle control missing: " + id +
                                                       " scale=" + std::to_string(scale) +
                                                       " compact=" + std::to_string(compact));
            const auto box = test::ui_targets.at(id);
            require(box.enabled, "Particle control disabled: " + id);
            // Selectable rows extend half their spacing beyond the child's clip;
            // their visible center is the actual click target, including the first row.
            const float center_y = (box.minimum.y + box.maximum.y) * .5f;
            const bool visible =
                id.starts_with("picker-option:")
                    ? center_y >= box.clip_minimum.y && center_y <= box.clip_maximum.y
                    : box.minimum.y >= box.clip_minimum.y && box.maximum.y <= box.clip_maximum.y;
            if (visible) {
                io.AddMousePosEvent((box.minimum.x + box.maximum.x) * .5f,
                                    (box.minimum.y + box.maximum.y) * .5f);
                frame();
                const auto settled = test::ui_targets.at(id);
                if (settled.minimum.y == box.minimum.y && settled.maximum.y == box.maximum.y)
                    return;
                continue;
            }
            // Exercise the actual scrollbar input, including compact/high-DPI panes.
            io.AddMousePosEvent((box.clip_minimum.x + box.clip_maximum.x) * .5f,
                                (box.clip_minimum.y + box.clip_maximum.y) * .5f);
            frame();
            io.AddMouseWheelEvent(0, box.minimum.y < box.clip_minimum.y ? 1.f : -1.f);
            frame();
        }
        throw std::runtime_error("Particle control could not be reached by scrolling: " + id);
    };
    auto click = [&](const std::string& id) {
        point(id);
        io.AddMouseButtonEvent(0, true);
        frame();
        io.AddMouseButtonEvent(0, false);
        frame();
    };
    auto number = [&](const std::string& id, const char* value) {
        if (id.starts_with("particles:color_gradient:") && id.ends_with(":value")) {
            point(id);
            const auto box = test::ui_targets.at(id);
            // InputFloat3's group rectangle includes its label. Aim inside the
            // first lane explicitly rather than assuming the group's center is G.
            io.AddMousePosEvent(box.minimum.x + 10 * scale, (box.minimum.y + box.maximum.y) * .5f);
            frame();
            io.AddMouseButtonEvent(0, true);
            frame();
            io.AddMouseButtonEvent(0, false);
            frame();
        } else
            click(id);
        io.AddKeyEvent(ImGuiKey_LeftCtrl, true);
        io.AddKeyEvent(ImGuiKey_A, true);
        frame();
        io.AddKeyEvent(ImGuiKey_A, false);
        io.AddKeyEvent(ImGuiKey_LeftCtrl, false);
        frame();
        io.AddInputCharactersUTF8(value);
        frame();
        io.AddKeyEvent(ImGuiKey_Enter, true);
        frame();
        io.AddKeyEvent(ImGuiKey_Enter, false);
        frame();
    };
    auto key = [&](ImGuiKey value) {
        io.AddKeyEvent(value, true);
        frame();
        io.AddKeyEvent(value, false);
        frame();
    };
    auto choose = [&](const std::string& id, unsigned option) {
        click(id);
        key(ImGuiKey_Home);
        for (unsigned i = 0; i < option; ++i)
            key(ImGuiKey_DownArrow);
        key(ImGuiKey_Enter);
        frame();
    };
    const auto revision = editor.document()->revision();
    number("particles:spawn_rate", "48");
    const auto edited = editor.document()->source().document;
    require(edited.at("spawn_rate") == 48 && editor.document()->revision() == revision + 1 &&
                asset_detail::read_bytes(root / source, particle_effect_byte_limit) == disk,
            "Numeric input did not commit exactly one isolated draft history entry");
    click("particles:undo");
    require(editor.document()->source().document == original, "Particle UI Undo failed");
    click("particles:redo");
    require(editor.document()->source().document == edited, "Particle UI Redo failed");
    number("particles:lifetime", "30");
    require(editor.document()->source().document == edited && !editor.diagnostic().empty(),
            "Over-capacity emission edited the document instead of reporting an error");
    number("particles:cone_angle", "40");
    require(editor.document()->source().document.at("cone_angle") == 40,
            "Particle cone control did not edit the effect");
    const auto before_extended = editor.document()->source().document;
    for (unsigned shape = 1; shape < 4; ++shape) {
        choose("particles:shape", shape);
        require(unsigned(editor.document()->source().compiled().shape) == shape,
                "Particle shape combo selected the wrong origin");
        require(editor.document()->source().compiled().version == 2 &&
                    editor.document()->source().document.at("asset_id") == original.at("asset_id"),
                "Actual extended input did not promote legacy source with the same identity");
        if (shape != 1) {
            number("particles:shape_radius", "1.8");
            require(editor.document()->source().compiled().shape_radius == 1.8,
                    "Particle radius control did not commit meters");
        } else
            require(test::ui_targets.contains("particles:shape_size"),
                    "Box origin did not expose full XYZ dimensions");
    }
    number("particles:drag", "2");
    require(editor.document()->source().compiled().drag == 2, "Particle drag control failed");
    const auto before_invalid_drag = editor.document()->source().document;
    number("particles:drag", "-1");
    require(editor.document()->source().document == before_invalid_drag &&
                !editor.diagnostic().empty(),
            "Invalid drag entered source history");
    click("particles:size_curve:enable");
    frame();
    require(editor.document()->source().compiled().size_curve.size() == 2 &&
                !test::ui_targets.at("particles:size_start").enabled,
            "Size curve did not replace the editable fallback controls");
    const auto endpoint = editor.document()->source().document.at("size_curve")[0].at("id");
    click("particles:size_curve:add");
    frame();
    const auto middle = editor.document()->source().document.at("size_curve")[1].at("id");
    number("particles:size_curve:stop:1:value", ".6");
    number("particles:size_curve:stop:1:time", ".3");
    choose("particles:size_curve:stop:1:interpolation", 1);
    require(editor.document()->source().compiled().size_curve[1].value == .6 &&
                editor.document()->source().compiled().size_curve[1].time == .3 &&
                editor.document()->source().compiled().size_curve[1].hold &&
                editor.document()->source().document.at("size_curve")[1].at("id") == middle &&
                editor.document()->source().document.at("size_curve")[0].at("id") == endpoint,
            "Lifetime inputs replaced identities or failed interpolation/value edits");
    const auto curve = editor.document()->source().document;
    number("particles:size_curve:stop:1:time", "1");
    require(editor.document()->source().document == curve && !editor.diagnostic().empty(),
            "Duplicate endpoint time entered source history");
    click("particles:size_curve:stop:1:remove");
    frame();
    require(editor.document()->source().compiled().size_curve.size() == 2,
            "Remove stop retained an interior value");
    click("particles:undo");
    frame();
    require(editor.document()->source().document == curve,
            "Undo did not restore the exact curve stop identity/value");
    click("particles:color_gradient:enable");
    click("particles:color_gradient:add");
    frame();
    require(editor.document()->source().compiled().color_gradient.size() == 3 &&
                !test::ui_targets.at("particles:color_start").enabled,
            "Color gradient controls failed to own the lifetime values");
    number("particles:color_gradient:stop:1:value", ".9");
    choose("particles:color_gradient:stop:1:interpolation", 1);
    require(editor.document()->source().compiled().color_gradient[1].value[0] == .9f &&
                editor.document()->source().compiled().color_gradient[1].hold,
            "Actual gradient RGB/Step inputs failed to commit");
    const auto with_gradient = editor.document()->source().document;
    click("particles:color_gradient:enable");
    frame();
    require(editor.document()->source().compiled().color_gradient.empty() &&
                test::ui_targets.at("particles:color_start").enabled,
            "Disabled gradient did not restore fallback editing");
    click("particles:undo");
    frame();
    require(editor.document()->source().document == with_gradient,
            "Undo of disable lost gradient identity/history");
    const auto cpu_profile = editor.document()->source().document;
    const auto cpu_revision = editor.document()->revision();
    choose("particles:evaluation", 1);
    const auto gpu_profile = editor.document()->source().document;
    auto expected_gpu = cpu_profile;
    expected_gpu["version"] = 3;
    expected_gpu["profile"] = "gpu_mesh1";
    require(gpu_profile == expected_gpu && editor.document()->revision() == cpu_revision + 1,
            "Evaluation control lost source identity or created multiple history entries");
    click("particles:undo");
    require(editor.document()->source().document == cpu_profile,
            "Evaluation Undo lost the original CPU source");
    click("particles:redo");
    require(editor.document()->source().document == gpu_profile,
            "Evaluation Redo lost the GPU source");
    choose("particles:evaluation", 0);
    require(editor.document()->source().document == cpu_profile,
            "Evaluation control lost source/stop identity when returning to CPU");
    click("particles:program");
    click("picker-option:" + graph_shader.asset().str());
    const auto graph_profile = editor.document()->source().document;
    require(editor.document()->source().compiled().version == 4 &&
                editor.document()->source().graph_program()->shader.id == graph_shader.asset() &&
                graph_profile.at("asset_id") == cpu_profile.at("asset_id") &&
                graph_profile.at("size_curve") == cpu_profile.at("size_curve"),
            "Native graph selection did not preserve effect/stop identity");
    const auto graph_revision = editor.document()->revision();
    number("particles:graph-size", ".4");
    require(editor.document()->source().graph_program()->maximum_size == .4 &&
                editor.document()->revision() == graph_revision + 1,
            "Native graph bound edit did not form one source history entry");
    click("particles:undo");
    require(editor.document()->source().document == graph_profile,
            "Graph bound Undo changed its program selection");
    choose("particles:evaluation", 0);
    require(editor.document()->source().compiled().version == 2 &&
                editor.document()->source().graph_program()->shader.id == graph_shader.asset(),
            "Native CPU switch discarded the inactive graph selection");
    choose("particles:evaluation", 2);
    require(editor.document()->source().document == graph_profile,
            "Native graph switch did not restore the retained selection");
    const auto alpha_revision = editor.document()->revision();
    choose("particles:evaluation", 3);
    const auto alpha_profile = editor.document()->source().document;
    require(editor.document()->source().compiled().version == 5 &&
                alpha_profile.at("version") == 5 && alpha_profile.at("profile") == "cpu_alpha1" &&
                alpha_profile.at("program") == graph_profile.at("program") &&
                alpha_profile.at("size_curve") == graph_profile.at("size_curve") &&
                editor.document()->revision() == alpha_revision + 1,
            "Native sorted-alpha choice lost settings or created multiple history entries");
    click("particles:undo");
    require(editor.document()->source().document == graph_profile,
            "Native sorted-alpha Undo changed the prior graph source");
    click("particles:redo");
    require(editor.document()->source().document == alpha_profile,
            "Native sorted-alpha Redo lost its source selection");
    const auto opacity_revision = editor.document()->revision();
    click("particles:opacity");
    require(editor.document()->source().compiled().version == 6 &&
                editor.document()->source().document.at("program") == graph_profile.at("program") &&
                editor.document()->revision() == opacity_revision + 1,
            "Native opacity activation lost retained settings or one-step history");
    number("particles:opacity_end", ".25");
    require(editor.document()->source().compiled().opacity_end == .25,
            "Native end-opacity input did not commit");
    click("particles:opacity_curve:enable");
    click("particles:opacity_curve:add");
    number("particles:opacity_curve:stop:1:value", ".75");
    choose("particles:opacity_curve:stop:1:interpolation", 1);
    const auto opacity_source = editor.document()->source().document;
    require(editor.document()->source().compiled().opacity_curve.size() == 3 &&
                editor.document()->source().compiled().opacity_curve[1].value == .75 &&
                editor.document()->source().compiled().opacity_curve[1].hold,
            "Native opacity curve inputs did not share source/history ownership");
    number("particles:opacity_curve:stop:1:time", "1");
    require(editor.document()->source().document == opacity_source && !editor.diagnostic().empty(),
            "Invalid opacity stop entered native source history");
    const auto billboard_revision = editor.document()->revision();
    click("particles:billboards");
    frame(); // Reconcile widgets that used the prior frame's compiled values.
    const auto billboard_source = editor.document()->source().document;
    require(editor.document()->source().compiled().version == 7 &&
                editor.document()->source().compiled().mesh == engine_primitive(8) &&
                billboard_source.at("mesh") == opacity_source.at("mesh") &&
                billboard_source.at("opacity_curve") == opacity_source.at("opacity_curve") &&
                editor.document()->revision() == billboard_revision + 1 &&
                !test::ui_targets.at("particles:mesh").enabled,
            "Native alignment lost retained source/history or enabled the fixed Quad picker");
    click("particles:undo");
    require(editor.document()->source().document == opacity_source &&
                test::ui_targets.at("particles:mesh").enabled,
            "Native alignment Undo did not restore the original source and Mesh control");
    click("particles:redo");
    require(editor.document()->source().document == billboard_source,
            "Native alignment Redo changed retained settings");
    choose("particles:evaluation", 0);
    frame();
    require(editor.document()->source().compiled().version == 2 &&
                editor.document()->source().document.at("billboard_enabled").get<bool>() &&
                test::ui_targets.at("particles:mesh").enabled,
            "Ordinary CPU applied retained alignment or disabled its Mesh selection");
    choose("particles:evaluation", 3);
    frame();
    require(editor.document()->source().document == billboard_source &&
                !test::ui_targets.at("particles:mesh").enabled,
            "CPU sorted-alpha did not restore retained alignment");
    click("particles:billboards");
    require(editor.document()->source().compiled().version == 6 &&
                editor.document()->source().document.at("opacity_curve") ==
                    opacity_source.at("opacity_curve"),
            "Disabling native alignment lost opacity or failed to restore Mesh projection");
    choose("particles:evaluation", 1);
    frame();
    const auto before_gpu_alignment = editor.document()->source().document;
    click("particles:billboards");
    frame();
    const auto gpu_alignment = editor.document()->source().document;
    require(editor.document()->source().compiled().version == 8 &&
                gpu_alignment.at("gpu_billboard_enabled").get<bool>() &&
                gpu_alignment.at("mesh") == opacity_source.at("mesh") &&
                gpu_alignment.at("opacity_curve") == opacity_source.at("opacity_curve") &&
                !test::ui_targets.at("particles:mesh").enabled,
            "GPU native alignment did not use its selected backend and retained source owner");
    click("particles:undo");
    require(editor.document()->source().document == before_gpu_alignment,
            "GPU native alignment Undo changed the older source representation");
    click("particles:redo");
    require(editor.document()->source().document == gpu_alignment,
            "GPU native alignment Redo changed its source state");
    choose("particles:evaluation", 2);
    frame();
    require(editor.document()->source().compiled().version == 9 &&
                editor.document()->source().compiled().program &&
                !test::ui_targets.at("particles:mesh").enabled,
            "Graph native selection did not restore retained GPU alignment");
    click("particles:billboards");
    frame();
    require(editor.document()->source().compiled().version == 4 &&
                !editor.document()->source().document.at("gpu_billboard_enabled").get<bool>() &&
                test::ui_targets.at("particles:mesh").enabled,
            "Disabling native GPU alignment did not restore the authored Mesh projection");
    choose("particles:evaluation", 3);
    frame();
    require(editor.document()->source().compiled().version == 6 &&
                editor.document()->source().document.at("version").get<unsigned>() == 8 &&
                editor.document()->source().document.at("opacity_curve") ==
                    opacity_source.at("opacity_curve"),
            "Returning to CPU opacity discarded retained GPU source settings");
    for (unsigned i = 0; i < 16 && editor.document()->source().document != alpha_profile; ++i)
        editor.undo();
    frame();
    require(editor.document()->source().document == alpha_profile,
            "Native opacity Undo did not restore the exact earlier alpha source");
    click("particles:undo");
    // Keep the original lifecycle/close checks applicable, including start-size editing.
    for (unsigned i = 0; i < 32 && editor.document()->source().document != before_extended; ++i)
        editor.undo();
    require(editor.document()->source().document == before_extended,
            "Extended particle edits did not return to the exact prior history state");
    frame();
    frame();
    number("particles:preview_time", "1.25");
    require(editor.preview_time() == 1.25 && !editor.preview_playing(),
            "Preview seek did not pause at the requested time: actual=" +
                std::to_string(editor.preview_time()) + " scale=" + std::to_string(scale) +
                " compact=" + std::to_string(compact) + " diagnostic=" + editor.diagnostic());
    const auto source_before_preview = editor.document()->source().document;
    click("particles:preview_play");
    require(editor.preview_playing(), "Particle preview Play did not start");
    const auto playing_time = editor.preview_time();
    for (unsigned i = 0; i < 5; ++i)
        frame();
    require(editor.preview_time() > playing_time, "Particle preview clock did not advance");
    click("particles:preview_play");
    const auto paused_time = editor.preview_time();
    frame();
    frame();
    require(editor.preview_time() == paused_time && !editor.preview_playing(),
            "Paused preview reads advanced emission age");
    number("particles:preview_time", "121");
    require(editor.preview_time() == paused_time && !editor.diagnostic().empty(),
            "Unsupported preview seek was accepted");
    click("particles:preview_reset");
    require(editor.preview_time() == 0 && !editor.preview_playing() &&
                editor.document()->source().document == source_before_preview,
            "Preview reset mutated effect values/history or retained playback");
    click("particles:preview_play");
    locked = true;
    frame();
    require(!editor.preview_playing() && !test::ui_targets.at("particles:save").enabled,
            "Locked document retained playback or enabled publication");
    locked = false;
    frame();
    click("particles:save");
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(15);
    do {
        frame();
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    } while (editor.pending() && std::chrono::steady_clock::now() < deadline);
    require(!editor.dirty() && editor.diagnostic().empty(),
            "Particle UI publication failed: " + editor.diagnostic());
    number("particles:spawn_rate", "1");
    number("particles:lifetime", "1");
    number("particles:capacity", "1");
    require(editor.document()->source().compiled().capacity == 1 && editor.diagnostic().empty(),
            "Inactive burst count prevented a valid capacity-one continuous effect");
    for (unsigned i = 0; i < 3; ++i)
        click("particles:undo");
    require(!editor.dirty(), "Undo of capacity controls retained source dirtiness");
    editor.draw_preview = {};
    click("particles:attach");
    require(scene.entity(actor).get<ParticleEmitter>().effect.id ==
                    editor.document()->source().asset() &&
                scene.undo() && scene.document() == initial_scene,
            "Particle effect assignment did not use Scene Undo");
    number("particles:size_start", "0.2");
    click("particles:undo");
    require(!editor.dirty(), "Undo to the published source retained a false rebuild requirement");
    click("particles:redo");
    require(editor.dirty(), "Redo of a source edit lost its draft dirtiness");
    editor.request_close();
    frame();
    frame();
    click("particles:close_cancel");
    require(editor.is_open() && editor.dirty() && editor.close_cancelled,
            "Dirty Particle close Cancel lost the document");
    editor.request_close();
    frame();
    frame();
    click("particles:close_discard");
    require(!editor.is_open() && previews > 0 && releases > 0,
            "Particle discard retained document/preview ownership");
    ImGui::DestroyContext();
}
} // namespace
int main(int argc, char** argv) {
    try {
        require(argc == 2, "Need particle UI scratch root");
        const auto scratch = std::filesystem::absolute(argv[1]);
        for (float scale : {1.f, 1.5f, 2.f})
            for (bool compact : {false, true}) {
                const auto root = scratch / AssetId::generate().str();
                exercise(root, scale, compact);
                // The exercise's project writer and resource owners have closed.
                std::filesystem::remove_all(root);
            }
        std::cout
            << "Particle actual mouse/key controls at100/150/200%, full/compact panes, "
               "history, preview clock isolation, publication, attach and dirty close passed\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
