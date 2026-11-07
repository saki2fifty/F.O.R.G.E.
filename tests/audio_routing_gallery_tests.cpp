#include "asset_bytes.hpp"
#include "audio_source_access.hpp"
#include "runtime_package.hpp"
#include "standalone_manifest.hpp"
#include <array>
#include <cmath>
#include <forge/audio.hpp>
#include <forge/authoring.hpp>
#include <forge/sound_cue.hpp>
#include <forge/game_settings.hpp>
#include <forge/project.hpp>
#include <forge/runtime.hpp>
#include <fstream>
#include <iostream>
#include <random>
using namespace forge;
namespace {
constexpr auto example = "Scenes/191-audio-routing.scene.json";
void require(bool value, const char* message) {
    if (!value)
        throw std::runtime_error(message);
}
Json read(const std::filesystem::path& path) {
    std::ifstream input(path);
    require(bool(input), "Audio routing Gallery document missing");
    return Json::parse(input);
}
AudioRouting routes() {
    return {{{"rain", "Rain", .5f, "effects", {{"music", .25f}}},
             {"room", "Room", .75f, "rain", {{"dialogue", .5f}}}}};
}
void author(const std::filesystem::path& root) {
    require(!std::filesystem::exists(root / example), "Audio routing Gallery already exists");
    ProjectLease lease(root);
    ProjectSettings project(root);
    auto settings = project.document();
    settings["game"]["audio"]["routing"] = audio_routing_json(routes());
    project.save(std::move(settings));
    EngineContext engine;
    Scene scene(engine.world());
    auto document = duplicate_scene_asset(read(root / "Scenes/33-audio-mixer.scene.json"),
                                          AssetId::generate(), scene.schema());
    for (auto& row : document["entities"]) {
        auto& values = row["components"];
        if (!values.contains("forge.audio_source"))
            continue;
        auto& sound = values["forge.audio_source"];
        const auto bus = sound.at("bus").get<unsigned>();
        sound["custom_bus"] = bus == 1 ? "rain" : bus == 2 ? "room" : "";
        row["name"] = bus == 1   ? "Rain - Effects output plus Music send"
                      : bus == 2 ? "Room - Rain output plus Dialogue send"
                                 : "Stock Dialogue comparison";
    }
    scene.reset(document);
    scene.save(root / example);
    auto catalog = AssetCatalog::open_project(root);
    (void)catalog.add_scene(example);
    catalog.save(AssetCatalog::project_index(root));
}
struct Game {
    EngineContext engine;
    Scene scene;
    RuntimeSimulation simulation;
    std::shared_ptr<AudioRuntime> audio;
    Game(const std::filesystem::path& root, const Json& document, AudioRouting routing)
        : engine(
              WorldRole::Runtime, false,
              {audio_module({root, AudioOutput::Offline, true, 1, {1, 1, 1}, std::move(routing)})}),
          scene(engine.world()), simulation(engine.world(), scene),
          audio(std::static_pointer_cast<AudioRuntime>(engine.services().audio())) {
        scene.restore_snapshot(document);
        simulation.reset_presentation();
        simulation.sync_audio();
        require(audio->wait_for_sources(std::chrono::seconds(5)),
                "Gallery audio failed to prepare");
        simulation.audio_paused(false);
    }
};
void verify(const std::filesystem::path& gallery, const std::filesystem::path& scratch) {
    const auto root = std::filesystem::absolute(scratch) / AssetId::generate().str();
    const auto source = root / "source", original = root / "package", moved = root / "relocated";
    std::filesystem::create_directories(root);
    std::filesystem::copy(gallery, source, std::filesystem::copy_options::recursive);
    if (!std::filesystem::exists(source / example))
        author(source); // Detached scratch proof before any one-time product authoring.
    const auto document = read(source / example);
    const auto authored_routing =
        audio_routing_settings(ProjectSettings(source).document().at("game").at("audio"));
    require(authored_routing == routes(),
            "Gallery routing defaults differ from the numeric example");
    EngineContext inspector;
    Scene schema_scene(inspector.world());
    const auto catalog = AssetCatalog::open_project(source);
    const auto id = document.at("asset_id").get<AssetId>();
    require(catalog.records().contains(id) && catalog.records().at(id).source == example,
            "Routing Scene catalog identity is missing");
    const std::array roots{id};
    {
        ProjectLease lease(source);
        prepare_runtime_content_catalog(lease, roots, {}, schema_scene.schema());
    }
    const RuntimePackageTarget target{"windows", "d3d12"};
    package_runtime_content(source, original, roots, target);
    std::filesystem::rename(original, moved);
    const auto packaged = open_runtime_content(moved, target);
    const auto game_document = read(moved / packaged.records().at(id).source);
    for (const auto& file : std::filesystem::recursive_directory_iterator(moved))
        require(file.path().extension() != ".wav",
                "Routing package retained a development recording");

    auto oracle_document = document;
    std::map<std::string, float> gains;
    unsigned sounds = 0;
    for (auto& row : oracle_document["entities"]) {
        auto& values = row["components"];
        if (!values.contains("forge.audio_source"))
            continue;
        ++sounds;
        auto& sound = values["forge.audio_source"];
        const auto key = sound.value("custom_bus", std::string{});
        const float gain = sound.at("gain").get<float>();
        gains.emplace(row.at("id").get<std::string>(), gain);
        // Independent explicit path sums with all four terminal faders at1.
        const float weight = key == "rain"   ? .5f * (1 + .25f)
                             : key == "room" ? .75f * (.5f * (1 + .25f) + .5f)
                                             : 1;
        sound["custom_bus"] = "";
        sound["bus"] = 0;
        sound["gain"] = gain * weight;
    }
    require(sounds == 3, "Routing Gallery requires three independently pitched emitters");
    Game authored(source, document, authored_routing), oracle(source, oracle_document, {});
    std::filesystem::rename(source, root / "source-hidden");
    Game exported(moved, game_document,
                  authored_routing); // Open only after all authoring paths disappear.
    double energy = 0;
    auto compare = [&](unsigned quanta) {
        for (unsigned q = 0; q < quanta; ++q) {
            std::array<float, 512> a{}, b{}, expected{};
            authored.audio->read_offline(a);
            exported.audio->read_offline(b);
            oracle.audio->read_offline(expected);
            require(a == b, "Source-hidden relocated routing PCM differs from authored output");
            for (std::size_t i = 0; i < a.size(); ++i) {
                require(std::isfinite(b[i]) && std::abs(b[i] - expected[i]) < 1e-6f,
                        "Gallery custom routes differ from independent stock-gain PCM oracle");
                energy += double(b[i]) * b[i];
            }
        }
    };
    compare(256);
    require(energy > 1e-6 && exported.audio->status().at("voices") == 3,
            "Routing Gallery did not produce three audible voices");
    for (auto* game : {&authored, &exported})
        game->audio->mix_volumes(.5f, {.25f, .5f, .75f}, {{"rain", 1}, {"room", 1}});
    for (const auto& row : document["entities"]) {
        const auto id = row.at("id").get<std::string>();
        if (!gains.contains(id))
            continue;
        auto entity = oracle.scene.entity(id);
        auto sound = entity.get<AudioSource>();
        const auto key =
            row.at("components").at("forge.audio_source").value("custom_bus", std::string{});
        const float weight = key == "rain"   ? .5f + .25f * .25f
                             : key == "room" ? (.5f + .25f * .25f) + .5f * .75f
                                             : .75f;
        sound.gain = gains.at(id) * weight;
        entity.set(sound);
    }
    oracle.audio->refresh_sources();
    oracle.audio->master_volume(.5f);
    compare(128);
    require(exported.audio->status().at("tick") == 0 &&
                exported.audio->status().at("failed_sources") == 0,
            "Gallery live faders advanced fixed time or lost source admission");
    std::filesystem::remove_all(root);
    std::cout << "Original routing Gallery: independent three-tone PCM, live faders and relocated "
                 "cooked content with source hidden passed\n";
}
#include "sound_cue_gallery_tests.hpp"
} // namespace
int main(int argc, char** argv) {
    try {
        if (argc == 4 && std::string_view(argv[1]) == "--publish-sound-cues")
            publish_sound_cue_gallery(std::filesystem::absolute(argv[2]),std::filesystem::absolute(argv[3]));
        else if (argc == 4 && std::string_view(argv[1]) == "--sound-cues")
            verify_sound_cue_gallery(argv[2],argv[3]);
        else if (argc == 3 && std::string_view(argv[1]) == "--author-sound-cues")
            author_sound_cue_gallery(std::filesystem::absolute(argv[2]));
        else if (argc == 3 && std::string_view(argv[1]) == "--author")
            author(std::filesystem::absolute(argv[2]));
        else {
            require(argc == 3, "Need Gallery and private scratch, or --author ROOT");
            verify(argv[1], argv[2]);
        }
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
