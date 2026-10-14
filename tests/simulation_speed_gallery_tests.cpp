#include "particle_runtime.hpp"
#include "runtime_package.hpp"
#include "ui_test_renderer.hpp"
#include <forge/render_scene.hpp>
#include <forge/runtime.hpp>
#include <forge/runtime_ui.hpp>
#include <fstream>
#include <iostream>
using namespace forge;
namespace {
void check(bool value, const char* why) {
    if (!value)
        throw std::runtime_error(why);
}
Json read_json(const std::filesystem::path& path) {
    Json result;
    std::ifstream(path) >> result;
    return result;
}
std::vector<std::byte> read_font(const std::filesystem::path& path) {
    std::ifstream stream(path, std::ios::binary);
    std::string data{std::istreambuf_iterator<char>(stream), {}};
    return {reinterpret_cast<const std::byte*>(data.data()),
            reinterpret_cast<const std::byte*>(data.data() + data.size())};
}
struct Result {
    double y, x, particle_time;
};
Result exercise(const std::filesystem::path& root, const Json& source, double rate,
                const std::vector<std::byte>& font, bool ui_check) {
    EngineContext engine(
        WorldRole::Runtime, false,
        {physics_module({}, root), animation_module(root), particle_module(root), ui_module(root)});
    Scene scene(engine.world());
    scene.restore_snapshot(source);
    RuntimeSimulation simulation(engine.world(), scene);
    auto particles = particle_runtime(engine.world());
    auto animation = animation_runtime(engine.world());
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(15);
    while (!animation->prepare_initial_pose() || !particles->prepare()) {
        check(std::chrono::steady_clock::now() < deadline, "Gallery58 resource admission timeout");
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    auto physics = std::static_pointer_cast<PhysicsRuntime>(engine.services().physics());
    physics->synchronize(0);
    simulation.reset_presentation();
    EntityId bounce, animated;
    for (const auto& row : source.at("entities")) {
        if (row.at("name") == "Physics bounce")
            bounce = row.at("id").get<EntityId>();
        if (row.at("name") == "Transform animation")
            animated = row.at("id").get<EntityId>();
    }
    check(bounce && animated, "Gallery58 actual domain actors missing");
    RuntimeClock clock({100, .25, 8});
    clock.set_time_scale(rate);
    clock.resume(RuntimeClock::Time{});
    const auto frames = rate == 0 ? 100u : unsigned(100 / rate);
    for (unsigned i = 1; i <= frames; ++i)
        clock.advance(RuntimeClock::Time{} + std::chrono::milliseconds(10 * i), [&](float dt) {
            check(dt == .01f, "Gallery58 changed fixed physics/Flecs delta");
            simulation.tick(dt);
        });
    check(clock.tick() == (rate == 0 ? 0u : 100u) && clock.status().at("dropped_ticks") == 0,
          "Gallery58 scaled timeline incorrect");
    const auto capture = simulation.presentation(1);
    const auto render = extract_render_scene(capture);
    if (!render.diagnostics.empty() || render.particles.size() != 1 || render.meshes.size() != 3)
        std::cerr << "Gallery58 consumers: "
                  << Json{{"particles", render.particles.size()},
                          {"meshes", render.meshes.size()},
                          {"cameras", render.cameras.size()},
                          {"lights", render.lights.size()},
                          {"diagnostics", render.diagnostics.size()}}
                         .dump()
                  << '\n';
    for (const auto& diagnostic : render.diagnostics)
        std::cerr << diagnostic_json(diagnostic).dump() << '\n';
    check(render.diagnostics.empty() && render.particles.size() == 1 && render.meshes.size() == 3 &&
              render.cameras.size() == 1 && render.lights.size() == 1,
          "Gallery58 visual consumers incomplete");
    const Result result{scene.entity(bounce.str()).get<LocalTranslation>().y,
                        scene.entity(animated.str()).get<LocalTranslation>().x,
                        render.particles[0].time};
    if (rate == 0)
        check(result.y == 4 && result.x == -3 && result.particle_time == 0,
              "Zero rate advanced domain state");
    else
        check(result.y < 3.9 && result.y >= .45 && result.x > -2.9 &&
                  std::abs(result.particle_time - 1) < 1e-6,
              "Gallery58 physics/animation/particle progression missing");
    if (ui_check) {
        auto ui = std::static_pointer_cast<UiRuntime>(engine.services().ui());
        const auto state = ui->snapshot(scene, "gallery58", 1, clock.tick(), clock.paused());
        UiTestRenderer renderer;
        {
            UiPresenter presenter(renderer, root, font);
            presenter.reset("gallery58", 1);
            check(presenter.accept(state), "Gallery58 UI did not load");
            ui_protocol::CommandGate gate;
            gate.reset("gallery58", 1);
            constexpr const char* values[]{"0", "0.25", "0.5", "1", "2", "4"};
            double ui_time = 0;
            for (const auto size : {std::pair{420, 640}, std::pair{1200, 900}}) {
                presenter.update(++ui_time, size.first, size.second);
                presenter.render();
                for (unsigned i = 0; i != 6; ++i) {
                    auto* button = Rml::GetContext(0)->GetDocument(0)->GetElementById(
                        "speed" + std::to_string(i));
                    check(button && button->IsVisible(true), "Gallery58 speed button unavailable");
                    const auto p = button->GetAbsoluteOffset();
                    const auto b = button->GetBox().GetSize(Rml::BoxArea::Border);
                    check(p.x >= 0 && p.y >= 0 && p.x + b.x <= size.first &&
                              p.y + b.y <= size.second,
                          "Gallery58 speed button clipped");
                    presenter.mouse_move(int(p.x + 10), int(p.y + 10), 0);
                    presenter.mouse_button(0, true, 0);
                    presenter.mouse_button(0, false, 0);
                    presenter.update(++ui_time, size.first, size.second);
                    const auto command = presenter.pending_command();
                    check(command && command->at("command") == "TimeScale" &&
                              command->at("value") == values[i],
                          "Native Gallery58 click lost typed rate");
                    const auto before = clock.tick();
                    const auto ack = gate.dispatch(*command, [&](const Json& c) {
                        ui->command(
                            scene, c, [](const std::string&) {},
                            [&](double value) { clock.set_time_scale(value); });
                    });
                    check(ack.at("ok") && clock.time_scale() == std::stod(values[i]) &&
                              clock.tick() == before,
                          "Gallery58 click bypassed clock owner or drove a tick");
                    presenter.acknowledge(ack);
                }
            }
            check(renderer.draws > 0 && presenter.diagnostic().empty(),
                  "Gallery58 native UI presentation failed");
        }
        check(renderer.geometry.empty() && renderer.textures.empty(),
              "Gallery58 UI leaked resources");
    }
    return result;
}
void all_rates(const std::filesystem::path& root, const Json& source,
               const std::vector<std::byte>& font) {
    const auto expected = exercise(root, source, 1, font, false);
    for (double rate : {0., .25, .5, 2., 4.}) {
        const auto actual = exercise(root, source, rate, font, rate == 0);
        if (rate != 0)
            check(std::abs(actual.y - expected.y) < 1e-5 &&
                      std::abs(actual.x - expected.x) < 1e-5 &&
                      actual.particle_time == expected.particle_time,
                  "Equal virtual time changed domain integration at a different rate");
    }
}
} // namespace
int main(int argc, char** argv) {
    try {
        check(argc == 4, "Need Gallery, scratch root and font");
        const auto root = std::filesystem::absolute(argv[2]) / AssetId::generate().str();
        std::filesystem::create_directories(root.parent_path());
        std::filesystem::copy(argv[1], root, std::filesystem::copy_options::recursive);
        const auto source = read_json(root / "Scenes/58-simulation-speed.scene.json");
        const auto font = read_font(argv[3]);
        all_rates(root, source, font);
        const std::array roots{source.at("asset_id").get<AssetId>()};
        RuntimeUiInspector inspector = [&](AssetId id, std::stop_token) {
            UiTestRenderer renderer;
            UiPresenter p(renderer, root, font);
            return p.inspect_static({id});
        };
        {
            auto lease = std::make_shared<ProjectLease>(root);
            prepare_runtime_content_catalog(*lease, roots, inspector);
        }
        const auto exported = std::filesystem::path(root.string() + "-export");
        package_runtime_content(root, exported, roots, {"windows", "d3d12"}, {}, {}, {}, inspector);
        const auto moved = std::filesystem::path(exported.string() + "-relocated");
        std::filesystem::rename(exported, moved);
        const auto shipped = open_runtime_content(moved, {"windows", "d3d12"});
        check(shipped.records().size() == 4,
              "Gallery58 exported closure missing or duplicated assets");
        const auto original_catalog = AssetCatalog::open_project(root);
        for (const auto& [id, record] : original_catalog.records())
            if (record.type == ParticleEffectAsset::type ||
                record.type == TransformAnimationAsset::type)
                check(!std::filesystem::exists(moved / record.source),
                      "Gallery58 leaked editable effect/clip source");
        check(read_json(root / "Scenes/58-simulation-speed.scene.json") == source,
              "Rate policy changed authored source");
        std::filesystem::rename(root, std::filesystem::path(root.string() + "-source-offline"));
        all_rates(moved, read_json(moved / shipped.records().at(roots[0]).source), font);
        std::cout << "Gallery58 real physics/animation/particles, native rate buttons and "
                     "relocated source-free export passed\n";
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
