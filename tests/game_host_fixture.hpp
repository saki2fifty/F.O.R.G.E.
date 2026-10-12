#pragma once
#include "game_host_source.hpp"
#include "material_graph_game_fixture.hpp"
#include "reference_game_workflow.hpp"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <forge/animation.hpp>
#include <forge/engine_assets.hpp>
#include <forge/render_components.hpp>
#include <numbers>
namespace forge::test {
struct GameHostFixture {
    std::filesystem::path output, project;
    unsigned stage = 0, frames = 0;
    bool pause_seen = false, packaged = false, mixed = false, prepare_only = false,
         expect_module = false, loads = false, velocity = false, contacts = false, volumes = false,
         slider = false, motors = false, position_drive = false, connected = false,
         swing_twist = false;
    std::array<double, 2> swing_maximum{}, swing_minimum{180, 180};
    bool reference = false, reference_reopen = false, reference_error = false,
         reference_missing = false, reference_settings = false;
    std::filesystem::path missing_source, missing_held;
    ReferenceGameWorkflow reference_workflow;
    std::string waiting = "startup";
    std::uint64_t ticket = 0;
    Uint64 started = SDL_GetTicks();
    Json original;
    void storage_probe(GameStorage& storage, const Json& settings, const InputMap& input,
                       AssetId startup) {
        if (reference) {
            atomic_write(output / "reference-storage.json",
                         Json{{"root", path_utf8(storage.root())}}.dump(2));
            return;
        }
        const GameSaveSchema schema{1,
                                    [startup](const GameSave& save) {
                                        check(save.scene == startup && save.data.at("marker") == 42,
                                              "Relocation changed saved fixture state");
                                    },
                                    {}};
        const auto slots = storage.slots();
        const bool reopened = std::find(slots.begin(), slots.end(), "relocation") != slots.end();
        const auto validate = [&](const Json& overrides) {
            (void)resolve_game_settings(settings, overrides, input);
        };
        if (reopened) {
            (void)storage.load("relocation", schema);
            check(storage.load_settings(validate).at("audio").at("master_volume") == .75,
                  "Relocation lost persistent user settings");
        } else {
            storage.save("relocation", {startup, {{"marker", 42}}}, schema);
            storage.save_settings({{"audio", {{"master_volume", .75}}}}, validate);
        }
        atomic_write(output / "storage-result.json", Json{{"root", path_utf8(storage.root())},
                                                          {"reopened", reopened},
                                                          {"scene", startup},
                                                          {"settings_persisted", true}}
                                                         .dump(2));
    }
    explicit GameHostFixture(int argc, char** argv) {
        reference = argc == 3 && (std::string_view(argv[1]) == "--reference" ||
                                  std::string_view(argv[1]) == "--reference-load" ||
                                  std::string_view(argv[1]) == "--reference-error" ||
                                  std::string_view(argv[1]) == "--reference-missing" ||
                                  std::string_view(argv[1]) == "--reference-settings");
        reference_settings = reference && std::string_view(argv[1]) == "--reference-settings";
        reference_missing = reference && std::string_view(argv[1]) == "--reference-missing";
        reference_error =
            reference && (std::string_view(argv[1]) == "--reference-error" || reference_missing);
        reference_reopen = reference && std::string_view(argv[1]) == "--reference-load";
        mixed = argc == 3 && std::string_view(argv[1]) == "--packaged-mixed";
        loads = argc == 3 && std::string_view(argv[1]) == "--packaged-loads";
        velocity = argc == 3 && std::string_view(argv[1]) == "--packaged-velocity";
        contacts = argc == 3 && std::string_view(argv[1]) == "--packaged-contacts";
        volumes = argc == 3 && std::string_view(argv[1]) == "--packaged-volumes";
        slider = argc == 3 && std::string_view(argv[1]) == "--packaged-slider";
        motors = argc == 3 && std::string_view(argv[1]) == "--packaged-motors";
        position_drive = argc == 3 && std::string_view(argv[1]) == "--packaged-position";
        connected = argc == 3 && std::string_view(argv[1]) == "--packaged-connected";
        swing_twist = argc == 3 && std::string_view(argv[1]) == "--packaged-swing";
        packaged = reference || mixed || loads || velocity || contacts || volumes || slider ||
                   motors || position_drive || connected || swing_twist ||
                   (argc == 3 && std::string_view(argv[1]) == "--packaged");
        prepare_only = argc == 3 && std::string_view(argv[1]) == "--prepare";
        if (argc != 2 && !packaged && !prepare_only)
            throw std::runtime_error("Game fixture requires output directory");
        output = std::filesystem::absolute(argv[argc == 3 ? 2 : 1]);
        std::filesystem::create_directories(output);
        if (packaged)
            return;
        project = make_game_host_project(output);
        add_graph_game_material(project, std::filesystem::absolute(argv[0]).parent_path() /
                                             "forge_shader_build.exe");
    }
    static void check(bool ok, const char* why) {
        if (!ok)
            throw std::runtime_error(why);
    }
    static void click(SDL_Window* window, float x, float y) {
        SDL_Event e{};
        e.type = SDL_EVENT_MOUSE_MOTION;
        e.motion.windowID = SDL_GetWindowID(window);
        e.motion.x = x;
        e.motion.y = y;
        check(SDL_PushEvent(&e), "Fixture mouse motion failed");
        e = {};
        e.type = SDL_EVENT_MOUSE_BUTTON_DOWN;
        e.button.windowID = SDL_GetWindowID(window);
        e.button.button = SDL_BUTTON_LEFT;
        e.button.down = true;
        e.button.x = x;
        e.button.y = y;
        check(SDL_PushEvent(&e), "Fixture button down failed");
        e.type = SDL_EVENT_MOUSE_BUTTON_UP;
        e.button.down = false;
        check(SDL_PushEvent(&e), "Fixture button up failed");
    }
    void capture(const char* name, Diligent::ITextureView* image, Diligent::IRenderDevice* device,
                 Diligent::IDeviceContext* context) {
        using namespace Diligent;
        auto desc = image->GetTexture()->GetDesc();
        desc.Usage = USAGE_STAGING;
        desc.BindFlags = BIND_NONE;
        desc.CPUAccessFlags = CPU_ACCESS_READ;
        RefCntAutoPtr<ITexture> staging;
        device->CreateTexture(desc, nullptr, &staging);
        check(bool(staging), "Standalone readback allocation failed");
        context->SetRenderTargets(0, nullptr, nullptr, RESOURCE_STATE_TRANSITION_MODE_NONE);
        CopyTextureAttribs copy;
        copy.pSrcTexture = image->GetTexture();
        copy.pDstTexture = staging;
        copy.SrcTextureTransitionMode = RESOURCE_STATE_TRANSITION_MODE_TRANSITION;
        copy.DstTextureTransitionMode = RESOURCE_STATE_TRANSITION_MODE_TRANSITION;
        context->CopyTexture(copy);
        context->WaitForIdle();
        MappedTextureSubresource data;
        context->MapTextureSubresource(staging, 0, 0, MAP_READ, MAP_FLAG_DO_NOT_WAIT, nullptr,
                                       data);
        check(data.pData != nullptr, "Standalone readback map failed");
        const auto* pixels = static_cast<const unsigned char*>(data.pData);
        const auto* center = pixels + (desc.Height / 2) * data.Stride + (desc.Width / 2) * 4;
        const auto* corner = pixels + (desc.Height - 8) * data.Stride + (desc.Width - 8) * 4;
        unsigned contrast = 0;
        for (unsigned c = 0; c < 3; ++c)
            contrast += unsigned(std::abs(int(center[c]) - int(corner[c])));
        std::size_t graph_pixels = 0;
        if (!reference && std::string_view(name) == "standalone-running.ppm")
            for (unsigned y = desc.Height / 4; y < desc.Height * 3 / 4; ++y)
                for (unsigned x = desc.Width / 4; x < desc.Width * 3 / 4; ++x) {
                    const auto* p = pixels + y * data.Stride + x * 4;
                    graph_pixels +=
                        p[1] > 30 && p[1] > unsigned(p[0]) + 10 && p[1] > unsigned(p[2]) + 5;
                }
        std::size_t imported_pixels = 0;
        std::size_t physics_actor_pixels = 0;
        std::size_t contact_actor_pixels = 0;
        if (loads || velocity || contacts || volumes || slider || motors || position_drive ||
            connected || swing_twist)
            for (unsigned y = 0; y < desc.Height; ++y)
                for (unsigned x = 0; x < desc.Width; ++x) {
                    const auto* p = pixels + y * data.Stride + x * 4;
                    // Authored physics examples use cyan actors. Sample the
                    // actual scene, whose moving actors need not cover its center.
                    physics_actor_pixels += p[1] > 30 && p[2] > 30 && p[1] > unsigned(p[0]) + 15 &&
                                            p[2] > unsigned(p[0]) + 15;
                    contact_actor_pixels +=
                        p[0] > 30 && p[0] > unsigned(p[1]) + 15 && p[0] > unsigned(p[2]) + 15;
                }
        if (mixed && std::string_view(name) == "standalone-running.ppm")
            for (unsigned y = desc.Height * 4 / 10; y < desc.Height * 6 / 10; ++y)
                for (unsigned x = desc.Width * 65 / 100; x < desc.Width * 9 / 10; ++x) {
                    const auto* pixel = pixels + y * data.Stride + x * 4;
                    unsigned difference = 0;
                    for (unsigned c = 0; c < 3; ++c)
                        difference += unsigned(std::abs(int(pixel[c]) - int(corner[c])));
                    imported_pixels += difference > 40;
                }
        std::ofstream file(output / name, std::ios::binary);
        file << "P6\n" << desc.Width << ' ' << desc.Height << "\n255\n";
        for (unsigned y = 0; y < desc.Height; ++y) {
            const auto* row = static_cast<const unsigned char*>(data.pData) + y * data.Stride;
            for (unsigned x = 0; x < desc.Width; ++x)
                file.write(reinterpret_cast<const char*>(row + x * 4), 3);
        }
        context->UnmapTextureSubresource(staging, 0, 0);
        check(bool(file), "Standalone capture write failed");
        if (!reference && !loads && !velocity && !contacts && !volumes && !slider && !motors &&
            !position_drive && !connected && !swing_twist)
            check(contrast > 30, "Standalone cube is not distinguishable from the background");
        if (loads || velocity || slider || motors || position_drive || connected || swing_twist) {
            check(physics_actor_pixels > 100,
                  "Authored physics actors are not visible in standalone rendering");
            atomic_write(output / "physics-render-result.json",
                         Json{{"cyan_actor_pixels", physics_actor_pixels}}.dump(2));
        }
        if (contacts || volumes) {
            check(physics_actor_pixels > 100 && contact_actor_pixels > 100,
                  "Physics actor and cyan geometry are not both visible");
            atomic_write(output / "physics-render-result.json",
                         Json{{"cyan_marker_pixels", physics_actor_pixels},
                              {"red_actor_pixels", contact_actor_pixels}}
                             .dump(2));
        }
        if (!reference && std::string_view(name) == "standalone-running.ppm") {
            check(graph_pixels > 100,
                  "The authored green graph material is not visible in standalone rendering");
            atomic_write(output / "material-graph-result.json",
                         Json{{"green_surface_pixels", graph_pixels}}.dump(2));
        }
        if (mixed && std::string_view(name) == "standalone-running.ppm")
            check(imported_pixels > 100,
                  "Imported textured mesh is not visible to the right of the built-in cube");
    }
    ~GameHostFixture() {
        if (!missing_held.empty()) {
            std::error_code error;
            std::filesystem::rename(missing_held, missing_source, error);
        }
    }
    void frame(GameSession& game, Diligent::ITextureView* image, Diligent::IRenderDevice* device,
               Diligent::IDeviceContext* context, SDL_Window* window, bool& running) {
        if (reference) {
            if (reference_missing && image && frames >= 3 && missing_held.empty()) {
                // Fault injection after startup integrity admission: emulate content disappearing.
                const auto root = std::filesystem::current_path() / "content";
                const auto catalog = AssetCatalog::open_project(root);
                const auto record = catalog.records().at(AssetId::parse(reference::level_scene));
                missing_source = ProjectPaths(root).resolve(record.source);
                missing_held = missing_source;
                missing_held += ".test-held";
                check(!std::filesystem::exists(missing_held), "Missing-content fixture collision");
                std::filesystem::rename(missing_source, missing_held);
            }
            if (image && ++frames >= 4)
                reference_workflow.frame(
                    game, window, output, reference_reopen,
                    reference_settings ? 2u : (reference_error ? 1u : 0u),
                    [&](const char* name) { capture(name, image, device, context); });
            return;
        }
        if (SDL_GetTicks() - started >= 90000) {
            atomic_write(output / "workflow-timeout.json", Json{{"stage", stage},
                                                                {"frames", frames},
                                                                {"waiting", waiting},
                                                                {"game", game.status()}}
                                                               .dump(2));
            throw std::runtime_error("Standalone workflow timed out at stage " +
                                     std::to_string(stage));
        }
        if (!image || ++frames < 4)
            return;
        if (swing_twist) {
            const auto tick = game.status().at("clock").at("tick").get<unsigned>();
            const auto scene = game.active().scene.document();
            Json angles = Json::object();
            for (const auto& row : scene.at("entities")) {
                const auto name = row.at("name").get<std::string>();
                if (name != "Wide socket body" && name != "Narrow socket body")
                    continue;
                const auto& q = row.at("components").at("forge.local_rotation");
                const auto angle =
                    std::abs(2 * std::atan2(q.at("z").get<double>(), q.at("w").get<double>()) *
                             180 / std::numbers::pi);
                angles[name] = angle;
                if (tick >= 30) {
                    const auto i = name == "Wide socket body" ? 0 : 1;
                    swing_maximum[i] = std::max(swing_maximum[i], angle);
                    swing_minimum[i] = std::min(swing_minimum[i], angle);
                }
            }
            if (tick < 240)
                return;
            check(angles.size() == 2 && game.active().physics()->status().at("joints") == 2,
                  "Exported swing/twist actors or native joints missing");
            check(swing_maximum[0] > 30 && swing_maximum[0] < 50.5 && swing_maximum[1] > 5 &&
                      swing_maximum[1] < 15.5 && swing_maximum[0] - swing_minimum[0] > 10 &&
                      swing_maximum[1] - swing_minimum[1] > 5,
                  "Exported gravity-driven swing/twist limits do not match independent bounds");
            atomic_write(output / "physics-swing-result.json", Json{{"tick", tick},
                                                                    {"angles", angles},
                                                                    {"maximum", swing_maximum},
                                                                    {"minimum", swing_minimum}}
                                                                   .dump(2));
            capture("physics-swing-running.ppm", image, device, context);
            running = false;
            return;
        }
        if (connected) {
            const auto tick = game.status().at("clock").at("tick").get<unsigned>();
            if (tick < 240)
                return;
            const auto scene = game.active().scene.document();
            std::map<std::string, Double3> positions;
            for (const auto& row : scene.at("entities")) {
                const auto name = row.at("name").get<std::string>();
                if (!name.starts_with("Ignored pair ") && !name.starts_with("Colliding pair "))
                    continue;
                if (row.at("components").contains("forge.physics_joint"))
                    continue;
                const auto& p = row.at("components").at("forge.local_translation");
                positions[name] = {p.at("x"), p.at("y"), p.at("z")};
            }
            check(positions.size() == 4 && game.active().physics()->status().at("joints") == 2,
                  "Exported connected-pair actors or joints missing");
            check(std::abs(positions.at("Ignored pair A")[0] + 2.3) < 1e-4 &&
                      std::abs(positions.at("Ignored pair B")[0] + 1.7) < 1e-4 &&
                      positions.at("Colliding pair B")[0] - positions.at("Colliding pair A")[0] >
                          .98,
                  "Exported mutual collision policy differs from authored choice");
            for (const auto& [name, p] : positions)
                check(std::abs(p[1] - .25) < .02, "Exported pair policy lost floor collision");
            atomic_write(output / "physics-connected-result.json",
                         Json{{"tick", tick}, {"positions", positions}}.dump(2));
            capture("physics-connected-running.ppm", image, device, context);
            running = false;
            return;
        }
        if (slider || motors || position_drive) {
            const auto tick = game.status().at("clock").at("tick").get<unsigned>();
            if (tick < 180)
                return;
            const auto scene = game.active().scene.snapshot();
            Json carriages = Json::array();
            for (const auto& row : scene.at("entities")) {
                const auto name = row.at("name").get<std::string>();
                const bool first = name == (position_drive ? "Return slider carriage"
                                            : motors       ? "Driven slider carriage"
                                                           : "Vertical slider carriage");
                if (!first && name != (position_drive ? "Return hinge carriage"
                                       : motors       ? "Driven hinge carriage"
                                                      : "Inclined slider carriage"))
                    continue;
                const auto& components = row.at("components");
                const auto& p = components.at("forge.local_translation");
                const auto& q = components.at("forge.local_rotation");
                const Double3 expected = position_drive
                                             ? (first ? Double3{-2.75, 4, 0} : Double3{2, 4, 0})
                                         : motors ? (first ? Double3{-.5, 4, 0} : Double3{2, 4, 0})
                                         : first ? Double3{-2, 3, 0}
                                                 : Double3{2 - std::sqrt(.5), 4 - std::sqrt(.5), 0};
                double error = 0;
                unsigned axis = 0;
                for (const auto* field : {"x", "y", "z"}) {
                    const double delta = p.at(field).get<double>() - expected[axis++];
                    error += delta * delta;
                }
                const LocalRotation rotation{q.at("x"), q.at("y"), q.at("z"), q.at("w")};
                const LocalRotation expected_rotation =
                    first ? LocalRotation{}
                          : LocalRotation{0, 0, float(std::sin(std::numbers::pi / 8)),
                                          float(std::cos(std::numbers::pi / 8))};
                const bool frame_matches =
                    (motors || position_drive)
                        ? std::abs(rotation.x) < 1e-6 && std::abs(rotation.y) < 1e-6 &&
                              std::abs(2 * std::atan2(rotation.z, rotation.w) -
                                       (first            ? 0
                                        : position_drive ? -std::numbers::pi / 4
                                                         : std::numbers::pi / 2)) < 1e-5
                        : equivalent(rotation, expected_rotation);
                check(error < .004 * .004 && frame_matches,
                      "Exported slider carriage lost its signed limit or locked frame");
                carriages.push_back({{"name", name}, {"components", components}});
            }
            check(carriages.size() == 2 && game.active().physics()->status().at("joints") == 2,
                  "Exported slider constraints or carriages missing");
            atomic_write(output / (position_drive ? "physics-position-result.json"
                                   : motors       ? "physics-motors-result.json"
                                                  : "physics-slider-result.json"),
                         Json{{"tick", tick}, {"carriages", carriages}}.dump(2));
            capture(position_drive ? "physics-position-running.ppm"
                    : motors       ? "physics-motors-running.ppm"
                                   : "physics-slider-running.ppm",
                    image, device, context);
            running = false;
            return;
        }
        if (volumes) {
            const auto tick = game.status().at("clock").at("tick").get<unsigned>();
            if (tick < 180)
                return;
            check(tick < 250, "Volume render missed the known occupancy interval");
            const auto scene = game.active().scene.snapshot();
            unsigned actors = 0;
            for (const auto& entity : scene.at("entities")) {
                const auto& components = entity.at("components");
                if (!components.contains("project.volume_probe"))
                    continue;
                ++actors;
                const auto& control = components.at("project.volume_probe");
                const auto ref = game.active().engine.world().reference(
                    game.active().scene.entity(entity.at("id")).id());
                const auto state = ref ? game.active().physics()->body_state(*ref) : std::nullopt;
                const double expected = -3 + double(1.f / 60) * tick;
                check(state && control.at("launched") == true && control.at("observed") == true &&
                          control.at("last_count") == 3 &&
                          std::abs(state->linear_velocity[0] - 1) < 1e-6 &&
                          std::abs(components.at("forge.local_translation").at("x").get<double>() -
                                   expected) < 2e-5,
                      "Exported volume observation failed independent native motion/count proof");
                atomic_write(output / "physics-volumes-result.json",
                             Json{{"tick", tick}, {"components", components}}.dump(2));
            }
            check(actors == 1, "Exported volume example actor count changed");
            capture("physics-volumes-running.ppm", image, device, context);
            running = false;
            return;
        }
        if (contacts) {
            const auto tick = game.status().at("clock").at("tick").get<unsigned>();
            if (tick < 90)
                return;
            unsigned begin_tick = 0;
            for (const auto& row : game.active().engine.services().diagnostics())
                if (row.at("text").get<std::string>().starts_with(
                        "Contact reaction: Begin | tick ")) {
                    check(!begin_tick, "Exported contact example repeated its native Begin");
                    begin_tick = row.at("context").at("tick").get<unsigned>();
                    check(row.at("text") ==
                              "Contact reaction: Begin | tick " + std::to_string(begin_tick),
                          "Exported contact tick disagrees with its diagnostic context");
                }
            check(begin_tick > 0 && begin_tick < tick, "Exported C++ contact Begin missing");
            const auto scene = game.active().scene.snapshot();
            unsigned actors = 0;
            for (const auto& entity : scene.at("entities")) {
                const auto& components = entity.at("components");
                if (!components.contains("project.contact_reaction"))
                    continue;
                ++actors;
                const auto& control = components.at("project.contact_reaction");
                const auto ref = game.active().engine.world().reference(
                    game.active().scene.entity(entity.at("id")).id());
                const auto state = ref ? game.active().physics()->body_state(*ref) : std::nullopt;
                const double expected = -3 + 2 * double(1.f / 60) * (2 * double(begin_tick) - tick);
                check(state && control.at("launched") == true && control.at("reacted") == true &&
                          control.at("begin_count") == 1 && control.at("end_count") == 1 &&
                          std::abs(state->linear_velocity[0] + 2) < 1e-6 &&
                          std::abs(components.at("forge.local_translation").at("x").get<double>() -
                                   expected) < 2e-5,
                      "Exported C++ sensor reaction failed independent queued-reversal proof");
                atomic_write(
                    output / "physics-contacts-result.json",
                    Json{{"begin_tick", begin_tick}, {"tick", tick}, {"components", components}}
                        .dump(2));
            }
            check(actors == 1, "Exported contact example actor count changed");
            capture("physics-contacts-running.ppm", image, device, context);
            running = false;
            return;
        }
        if (velocity) {
            if (game.status().at("clock").at("tick").get<unsigned>() < 180)
                return;
            Json proof = Json::object();
            const auto scene = game.active().scene.snapshot();
            for (const auto& entity : scene.at("entities")) {
                const auto& components = entity.at("components");
                if (!components.contains("project.velocity_control"))
                    continue;
                const auto& control = components.at("project.velocity_control");
                const auto channel = control.at("stop_channel").get<unsigned>();
                const auto runtime_entity = game.active().scene.entity(entity.at("id"));
                const auto ref = game.active().engine.world().reference(runtime_entity.id());
                const auto state = ref ? game.active().physics()->body_state(*ref) : std::nullopt;
                check(state && control.at("launched") == true &&
                          control.at("stopped") == (channel != 0),
                      "Exported C++ velocity example did not execute launch and timed stops");
                check(std::abs(state->linear_velocity[1] - (channel & 1 ? 0 : 1)) < 1e-6 &&
                          std::abs(state->angular_velocity[2] - (channel & 2 ? 0 : 1)) < 1e-6,
                      "Exported zero velocity command lost the unselected channel");
                proof[std::to_string(channel)] = components;
            }
            check(proof.size() == 4, "Exported C++ velocity example missing actors");
            const auto y = [&](const char* channel) {
                return proof.at(channel).at("forge.local_translation").at("y").get<double>();
            };
            check(std::abs(y("1") - 119. / 60) < 1e-5 && std::abs(y("1") - y("3")) < 1e-6 &&
                      y("0") > 2.9 && std::abs(y("0") - y("2")) < 1e-6,
                  "Exported C++ timed stop failed independent translation equation");
            atomic_write(output / "physics-velocity-result.json", proof.dump(2));
            capture("physics-velocity-running.ppm", image, device, context);
            running = false;
            return;
        }
        if (loads) {
            if (game.status().at("clock").at("tick").get<unsigned>() < 30)
                return;
            Json proof = Json::object();
            const auto scene = game.active().scene.snapshot();
            for (const auto& entity : scene.at("entities"))
                if (entity.at("components").contains("project.body_loads"))
                    proof[entity.at("name").get<std::string>()] = entity.at("components");
            check(proof.size() == 6, "Packaged C++ loads example missing actors");
            const double force = proof.at("Force - 1 kg").at("forge.local_translation").at("y"),
                         heavy = proof.at("Force - 2 kg").at("forge.local_translation").at("y"),
                         impulse = proof.at("Impulse - 1 kg").at("forge.local_translation").at("y");
            check(force > .7 && impulse > 2 && std::abs(force - 2 * heavy) < 1e-4,
                  "Exported C++ force/impulse or mass comparison did not run");
            const auto previous = game.active().simulation.presentation(0);
            for (const auto& row : previous.at("entities")) {
                const auto name = row.at("name").get<std::string>();
                if (name != "Torque - 1 kg" && name != "Angular impulse - 1 kg" &&
                    name != "Off-center impulse - 1 kg")
                    continue;
                const auto current = game.active().scene.entity(row.at("id")).get<WorldTransform>();
                double change = 0;
                for (unsigned i : {0u, 1u, 2u, 4u, 5u, 6u, 8u, 9u, 10u})
                    change +=
                        std::abs(row.at("world_affine").at(i).get<double>() - current.affine.m[i]);
                check(change > .0001,
                      "Exported C++ angular load did not rotate between fixed samples");
            }
            atomic_write(output / "physics-loads-result.json", proof.dump(2));
            capture("physics-loads-running.ppm", image, device, context);
            running = false;
            return;
        }
        if (stage == 0) {
            Json proof = {{"module_required", expect_module},
                          {"module_ticked", false},
                          {"mixed_content", mixed}};
            if (expect_module) {
                check(bool(game.active().engine.world().world().lookup("example.state")),
                      "Packaged gameplay module did not start");
                const auto diagnostics = game.active().engine.services().diagnostics();
                const bool ticked =
                    std::any_of(diagnostics.begin(), diagnostics.end(), [](const auto& d) {
                        return d.at("text").template get<std::string>().starts_with(
                            "Example tick=");
                    });
                if (!ticked) {
                    waiting = "native gameplay fixed tick";
                    return;
                }
                proof["module_ticked"] = true;
            }
            auto effective = game.active().scene.effective_document();
            if (mixed)
                for (const auto* name :
                     {"Animated actor", "Agent", "Speaker", "Imported renderable"})
                    check(std::count_if(
                              effective.at("entities").begin(), effective.at("entities").end(),
                              [&](const auto& entity) { return entity.at("name") == name; }) == 1,
                          "Mixed-content runtime lost an expected entity");
            for (const auto& entity : effective.at("entities")) {
                if (entity.at("name") == "Animated actor") {
                    const auto animation = animation_runtime(game.active().engine.world());
                    check(bool(animation), "Packaged animation runtime is unavailable");
                    const auto actor =
                        game.active().scene.entity(entity.at("id").get<std::string>());
                    // effective_document() describes scene data/transforms. The
                    // sampled pose belongs to AnimationRuntime, never authored ECS truth.
                    const auto pose = animation->presentation(actor.id(), 1);
                    if (pose.is_null() || pose.at("time").get<double>() <= 0) {
                        waiting = "animation sample after first fixed tick";
                        return;
                    }
                    proof["animation_time"] = pose.at("time");
                }
                if (entity.at("name") == "Agent") {
                    if (entity.at("world_affine")[3].get<double>() <= -8) {
                        waiting = "navigation agent movement";
                        return;
                    }
                    proof["navigation_x"] = entity.at("world_affine")[3];
                }
                if (entity.at("name") == "Speaker") {
                    const auto audio = std::static_pointer_cast<AudioRuntime>(
                        game.active().engine.services().audio());
                    check(audio && audio->status().at("voices").get<unsigned>() == 1,
                          "Packaged audio did not realize a voice");
                    proof["audio_voices"] = audio->status().at("voices");
                }
            }
            atomic_write(output / "runtime-content-result.json", proof.dump(2));
            waiting = "UI pause/resume and scene replacement";
            original = game.active().scene.snapshot();
            capture("standalone-running.ppm", image, device, context);
            click(window, 80, 40);
            stage = 1;
        } else if (stage == 1 && game.status().at("state") == "paused") {
            if (!pause_seen) {
                pause_seen = true;
                return; // Capture after the next UI snapshot reflects the click.
            }
            capture("standalone-paused.ppm", image, device, context);
            click(window, 80, 92);
            stage = 2;
        } else if (stage == 2 && game.status().at("state") == "running") {
            game.pause(RuntimeClock::Clock::now());
            original = game.active().scene.snapshot();
            auto broken = original;
            for (auto& e : broken.at("entities"))
                if (e.at("components").contains("forge.mesh_renderer"))
                    e["components"]["forge.mesh_renderer"]["mesh"] = AssetId::generate();
            ticket = game.prepare(broken);
            stage = 3;
        } else if (stage == 3) {
            bool failed = false;
            try {
                if (game.poll_preparation(ticket).ready)
                    throw std::logic_error("Invalid mesh was reported ready");
            } catch (const std::logic_error&) {
                throw;
            } catch (const std::runtime_error&) {
                failed = true;
            }
            if (failed) {
                check(game.active().scene.snapshot() == original,
                      "Failed graphical candidate changed the active scene");
                stage = 30; // Let the visible loading binding show the failed outcome.
            }
        } else if (stage == 30) {
            capture("standalone-failed-load-retained.ppm", image, device, context);
            ticket = game.prepare(original);
            stage = 31;
        } else if (stage == 31) {
            capture("standalone-loading.ppm", image, device, context);
            click(window, 80, 252);
            stage = 32;
        } else if (stage == 32) {
            if (game.loading_state().state != "cancelled")
                return;
            check(game.active().scene.snapshot() == original,
                  "Loading cancel changed active scene");
            capture("standalone-cancelled.ppm", image, device, context);
            ticket = game.prepare(original);
            stage = 4;
        } else if (stage == 4 && game.poll_preparation(ticket).ready) {
            game.activate(ticket, RuntimeClock::Clock::now(), false);
            stage = 5;
        } else if (stage == 5) {
            check(game.status().at("state") == "paused", "Replacement did not stay paused");
            capture("standalone-replaced.ppm", image, device, context);
            check(SDL_SetWindowSize(window, 800, 600), "Standalone resize failed");
            stage = 6;
        } else if (stage == 6) {
            int client_width = 0, client_height = 0;
            check(SDL_GetWindowSize(window, &client_width, &client_height),
                  "Standalone client size unavailable");
            if (client_width != 800 || client_height != 600)
                return;
            int width = 0, height = 0;
            check(SDL_GetWindowSizeInPixels(window, &width, &height),
                  "Standalone output size unavailable");
            const auto& desc = image->GetTexture()->GetDesc();
            if (desc.Width != unsigned(width) || desc.Height != unsigned(height))
                return;
            check(game.active().scene.snapshot() == original,
                  "Window resize changed authored scene state");
            capture("standalone-resized.ppm", image, device, context);
            atomic_write(output / "standalone-result.json",
                         Json{{"startup", true},
                              {"ui_pause_resume", true},
                              {"failed_gpu_candidate_retained", true},
                              {"prepared_replacement", true},
                              {"loading_ui_cancel", true},
                              {"resized", {{"width", width}, {"height", height}}},
                              {"final", game.status()}}
                             .dump(2));
            running = false;
        }
    }
};
} // namespace forge::test
