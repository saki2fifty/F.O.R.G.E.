#include "runtime_package.hpp"
#include <array>
#include <cmath>
#include <forge/audio.hpp>
#include <forge/authoring.hpp>
#include <forge/runtime.hpp>
#include <fstream>
#include <iostream>
#include <set>
#include <vector>
using namespace forge;
namespace {
void require(bool value, const char* message) {
    if (!value)
        throw std::runtime_error(message);
}
Json read(const std::filesystem::path& path) {
    Json value;
    std::ifstream(path) >> value;
    return value;
}
double power(AudioRuntime& audio) {
    std::vector<float> warm(4096 * 2), samples(4800 * 2);
    audio.read_offline(warm);
    audio.read_offline(samples);
    double sum = 0;
    for (float value : samples) {
        require(std::isfinite(value), "Exported audio produced nonfinite PCM");
        sum += double(value) * value;
    }
    return sum / samples.size();
}
} // namespace
int main(int argc, char** argv) {
    try {
        require(argc == 3, "Need Gallery and scratch roots");
        const auto scratch = std::filesystem::absolute(argv[2]) / AssetId::generate().str();
        const auto project = scratch / "project", output = scratch / "export";
        std::filesystem::create_directories(scratch);
        std::filesystem::copy(argv[1], project, std::filesystem::copy_options::recursive);
        const auto source = read(project / "Scenes/31-audio-falloff.scene.json");
        const auto cone_source = read(project / "Scenes/32-directional-audio.scene.json");
        const auto mix_source = read(project / "Scenes/33-audio-mixer.scene.json");
        const std::array roots{source.at("asset_id").get<AssetId>(),
                               cone_source.at("asset_id").get<AssetId>(),
                               mix_source.at("asset_id").get<AssetId>()};
        const auto clip = AssetId::parse("327101c1-e496-4293-b830-4c25b978607a");
        EngineContext author;
        Scene author_scene(author.world());
        {
            ProjectLease lease(project);
            prepare_runtime_content_catalog(lease, roots, {}, author_scene.schema());
            package_runtime_content(project, output, roots, {"windows", "d3d12"}, {}, {},
                                    author_scene.schema());
        }
        require(!std::filesystem::exists(output / "Assets/FalloffTone.wav"),
                "Export shipped the original WAV");
        const auto relocated = scratch / "relocated";
        std::filesystem::rename(output, relocated);
        std::filesystem::remove_all(project);
        const auto catalog = open_runtime_content(relocated, {"windows", "d3d12"});
        require(catalog.records().contains(clip), "Export omitted cooked AudioClip");
        const auto shipped = read(relocated / catalog.records().at(roots[0]).source);
        require(shipped == source, "Export changed authored Gallery values/identity");
        {
            EngineContext engine(WorldRole::Runtime, false,
                                 {audio_module({relocated, AudioOutput::Offline, true})});
            Scene scene(engine.world());
            RuntimeSimulation simulation(engine.world(), scene);
            scene.restore_snapshot(shipped);
            simulation.reset_presentation();
            simulation.sync_audio();
            simulation.audio_paused(false);
            auto audio = std::static_pointer_cast<AudioRuntime>(engine.services().audio());
            unsigned speakers = 0, listeners = 0;
            for (const auto& row : shipped.at("entities")) {
                auto entity = scene.entity(row.at("id"));
                if (entity.has<AudioListener>()) {
                    entity.set<LocalTranslation>({0, 0, 0});
                    entity.set<LocalRotation>({});
                    ++listeners;
                }
                if (entity.has<AudioSource>()) {
                    auto value = entity.get<AudioSource>();
                    require(value.clip.id == clip && value.attenuation < 3 && value.rolloff == 1,
                            "Cooked scene lost typed falloff parameters");
                    audio->stop_source(engine.world().reference(entity.id()).value());
                    ++speakers;
                }
            }
            require(speakers == 3 && listeners == 1, "Gallery emitters/listener missing");
            for (const auto& row : shipped.at("entities")) {
                auto entity = scene.entity(row.at("id"));
                if (!entity.has<AudioSource>())
                    continue;
                auto value = entity.get<AudioSource>();
                const auto mode = value.attenuation;
                entity.set<LocalTranslation>({0, 0, 2});
                simulation.tick(1.f / 60);
                const auto ref = engine.world().reference(entity.id()).value();
                audio->play(ref);
                simulation.tick(1.f / 60);
                const double baseline = power(*audio);
                require(baseline > .001, "Source-free exported clip did not play");
                entity.set<LocalTranslation>({0, 0, 6});
                simulation.tick(1.f / 60);
                const double ratio = power(*audio) / baseline;
                const double amplitude = mode == 1 ? .75 : 1. / 3;
                if (std::abs(ratio - amplitude * amplitude) >= .004)
                    std::cerr << "Exported mode " << mode << ": power ratio " << ratio
                              << ", expected " << amplitude * amplitude << '\n';
                require(std::abs(ratio - amplitude * amplitude) < .004,
                        "Exported falloff did not match the independent power equation");
                audio->stop_source(ref);
            }
        }
        {
            const auto shipped_cone = read(relocated / catalog.records().at(roots[1]).source);
            require(shipped_cone == cone_source,
                    "Directional export changed authored identity/settings");
            EngineContext engine(WorldRole::Runtime, false,
                                 {audio_module({relocated, AudioOutput::Offline, true})});
            Scene scene(engine.world());
            RuntimeSimulation simulation(engine.world(), scene);
            scene.restore_snapshot(shipped_cone);
            simulation.reset_presentation();
            simulation.sync_audio();
            simulation.audio_paused(false);
            flecs::entity speaker, listener;
            std::string parent;
            for (const auto& row : shipped_cone.at("entities")) {
                const auto entity = scene.entity(row.at("id"));
                if (entity.has<AudioSource>()) {
                    speaker = entity;
                    parent = row.at("parent");
                }
                if (entity.has<AudioListener>())
                    listener = entity;
            }
            require(speaker && listener && !parent.empty(),
                    "Directional Gallery hierarchy/listener missing");
            const auto value = speaker.get<AudioSource>();
            require(value.cone_enabled && value.inner_angle == 60 && value.outer_angle == 120 &&
                        value.outer_gain == .25f,
                    "Cooked scene lost public directional fields");
            const auto position = speaker.get<WorldTransform>().affine.point({0, 0, 0});
            listener.set<LocalTranslation>({position[0], position[1], position[2] - 6});
            listener.set<LocalRotation>({});
            simulation.tick(1.f / 60);
            auto audio = std::static_pointer_cast<AudioRuntime>(engine.services().audio());
            const double front = power(*audio);
            require(front > .001, "Relocated directional speaker did not play");
            scene.entity(parent).set<LocalRotation>({0, 1, 0, 0});
            simulation.tick(1.f / 60);
            require(std::abs(power(*audio) / front - .0625) < .004,
                    "Source-free parent rotation did not change actual angular volume");
        }
        {
            const auto shipped = read(relocated / catalog.records().at(roots[2]).source);
            require(shipped == mix_source, "Mixer export changed authored identity/channel routes");
            EngineContext engine(WorldRole::Runtime, false,
                                 {audio_module({relocated, AudioOutput::Offline, true})});
            Scene scene(engine.world());
            RuntimeSimulation simulation(engine.world(), scene);
            scene.restore_snapshot(shipped);
            simulation.reset_presentation();
            simulation.sync_audio();
            simulation.audio_paused(false);
            auto audio = std::static_pointer_cast<AudioRuntime>(engine.services().audio());
            std::vector<EntityRef> sources;
            for (const auto& row : shipped.at("entities")) {
                auto entity = scene.entity(row.at("id"));
                if (entity.has<AudioSource>()) {
                    sources.push_back(engine.world().reference(entity.id()).value());
                    audio->stop_source(sources.back());
                }
            }
            require(sources.size() == 3, "Relocated mixer gallery lost routed emitters");
            simulation.tick(1.f / 60);
            std::set<unsigned> routes;
            for (const auto ref : sources) {
                const unsigned bus = scene.entity(ref.entity.str()).get<AudioSource>().bus;
                require(bus >= 1 && bus <= 3 && routes.insert(bus).second,
                        "Relocated mixer route missing or duplicated");
                audio->mix_volumes(1, {1, 1, 1});
                audio->play(ref);
                simulation.tick(1.f / 60);
                const auto nominal = power(*audio);
                require(nominal > 1e-5, "Source-free routed emitter did not play");
                std::array<float, 3> channels{1, 1, 1};
                channels[bus - 1] = .5;
                audio->mix_volumes(.5, channels);
                require(std::abs(power(*audio) / nominal - .0625) < .004,
                        "Relocated mixer PCM did not multiply its channel and Master");
                audio->stop_source(ref);
                simulation.tick(1.f / 60);
            }
        }
        std::filesystem::remove_all(scratch);
        std::cout << "Original Gallery31 cooked audio/identity and three falloff modes play "
                     "after source removal and relocation\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
