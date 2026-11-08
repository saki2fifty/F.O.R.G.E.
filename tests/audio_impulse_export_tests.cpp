#include "asset_bytes.hpp"
#include "asset_storage.hpp"
#include "game_export.hpp"
#include "standalone_manifest.hpp"
#include <cmath>
#include <forge/game_content.hpp>
#include <forge/game_settings.hpp>
#include <forge/project.hpp>
#include <forge/runtime_world.hpp>
#include <iostream>
#include <thread>

namespace {
using namespace forge;
void check(bool condition, const char* message) {
    if (!condition)
        throw std::runtime_error(message);
}
std::vector<float> samples(const std::filesystem::path& root, const Json& settings) {
    const auto& values = settings.at("game").at("audio");
    AudioConfig config;
    config.project = root;
    config.output = AudioOutput::Offline;
    config.required = true;
    config.master_volume = values.value("master_volume", 1.f);
    config.channel_volumes = audio_channel_volumes(values);
    config.routing = audio_routing_settings(values);
    config.bus_volumes = audio_bus_volumes(values);
    RuntimeWorld world({}, PhysicsConfig{}, config, root, false);
    world.scene.restore_snapshot(
        load_game_scene(root, {settings.at("startup_scene").at("asset").get<AssetId>()}));
    world.engine.world().modules().scene_ready();
    world.simulation.reset_presentation();
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (!world.prepare_audio_sources()) {
        if (std::chrono::steady_clock::now() >= deadline)
            throw std::runtime_error("Impulse sample audio preparation timed out");
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    world.simulation.audio_paused(false);
    auto audio = std::static_pointer_cast<AudioRuntime>(world.engine.services().audio());
    std::vector<float> result(2 * 48000);
    audio->read_offline(result);
    return result;
}
} // namespace
int main(int argc, char** argv) {
    try {
        check(argc == 4, "Need sample project, scratch and font");
        const auto scratch = std::filesystem::absolute(argv[2]) / AssetId::generate().str();
        const auto source = scratch / "Source", kit = scratch / "Kit", output = scratch / "Game";
        std::filesystem::create_directories(scratch);
        std::filesystem::copy(std::filesystem::absolute(argv[1]), source,
                              std::filesystem::copy_options::recursive);
        const auto settings = ProjectSettings(source).document();
        const auto& audio = settings.at("game").at("audio");
        const auto responses = audio_bus_impulse_assets(audio_routing_settings(audio));
        check(responses.size() == 1, "Configured example has no unique impulse response");
        const auto expected = samples(source, settings);
        auto bypassed = settings;
        bypassed["game"]["audio"]["routing"]["buses"][0]["effects"]["impulse_response"]["wet"] = 0;
        const auto dry = samples(source, bypassed);
        double energy = 0, stereo_difference = 0, effect_difference = 0;
        for (std::size_t i = 0; i < expected.size(); ++i) {
            check(std::isfinite(expected[i]), "Example produced nonfinite PCM");
            energy += double(expected[i]) * expected[i];
            effect_difference += std::abs(double(expected[i]) - dry[i]);
            if (!(i & 1))
                stereo_difference += std::abs(double(expected[i]) - expected[i + 1]);
        }
        check(energy > .01 && stereo_difference > .01 && effect_difference > .01,
              "Example failed to exercise audible stereo impulse processing");
        std::filesystem::create_directories(kit / "resources/ui");
        std::filesystem::copy_file(argv[3], kit / "resources/ui/LatoLatin-Regular.ttf");
        // Inventory-only executable fixture; no platform execution claim.
        asset_storage::replace(kit / "forge_game.exe", "test executable bytes");
        const RuntimePackageTarget target{"windows", "d3d12"};
        Json runtime{{"format", "forge.runtime-kit"},
                     {"version", 1},
                     {"executable", "forge_game.exe"},
                     {"target", {{"platform", "windows"}, {"backend", "d3d12"}}},
                     {"engine",
                      {{"profile", "static-core"},
                       {"sdk_fingerprint", std::string(64, 'a')},
                       {"source_commit", std::string(40, 'b')},
                       {"build_id", "260923-000066"},
                       {"runtime_audio_routing", audio_routing_profile_version},
                       {"runtime_audio_bus_effects", audio_bus_effects_profile_version}}},
                     {"files", Json::object()}};
        for (const auto& entry : std::filesystem::recursive_directory_iterator(kit))
            if (entry.is_regular_file()) {
                const auto bytes = asset_detail::read_bytes(entry.path(), 16 * 1024 * 1024);
                runtime["files"][path_utf8(entry.path().lexically_relative(kit))] = {
                    {"bytes", bytes.size()}, {"sha256", asset_detail::content_digest(bytes)}};
            }
        const auto write_kit = [&] {
            asset_storage::replace(kit / "forge.runtime-kit.json", runtime.dump());
        };
        write_kit();
        EngineContext authoring;
        Scene schema(authoring.world());
        GameExportRequest request;
        request.destination = output;
        request.runtime_kit = kit;
        request.reference_schema = schema.schema();
        const auto reject_export = [&] {
            bool rejected = false;
            try {
                ProjectLease lease(source);
                (void)export_standalone_game(lease, request);
            } catch (const std::exception& error) {
                if (std::string_view(error.what()).find("impulse responses require") ==
                    std::string_view::npos)
                    throw;
                rejected = true;
            }
            check(rejected, "A kit without impulse support accepted the example");
        };
        reject_export();
        check(!std::filesystem::exists(output), "Failed first export published a distribution");
        runtime["engine"]["runtime_audio_bus_impulse"] = audio_bus_impulse_profile_version;
        write_kit();
        {
            ProjectLease lease(source);
            (void)export_standalone_game(lease, request);
        }
        const auto manifest = asset_storage::read(output / "forge.standalone.json");
        runtime["engine"].erase("runtime_audio_bus_impulse");
        write_kit();
        reject_export();
        check(asset_storage::read(output / "forge.standalone.json") == manifest,
              "Rejected kit replaced the last good impulse distribution");
        std::filesystem::rename(source, scratch / "HiddenSource");
        const auto relocated = scratch / "Relocated";
        std::filesystem::rename(output, relocated);
        const auto admitted =
            open_standalone_distribution(relocated, target, "static-core", std::string(64, 'a'));
        const auto catalog = open_runtime_content(admitted.content, target);
        check(catalog.records().size() == 3 && catalog.records().contains(responses[0].id),
              "Project-only impulse reference is missing from the runtime closure");
        check(admitted.settings.at("game").at("audio") == settings.at("game").at("audio"),
              "Export changed the saved impulse settings");
        check(samples(admitted.content, admitted.settings) == expected,
              "Source-hidden relocated example PCM differs from authored playback");
        std::cout << Json{{"catalog_records", catalog.records().size()},
                          {"pcm_frames", expected.size() / 2},
                          {"source_hidden", true},
                          {"exact_pcm", true},
                          {"stereo_difference", stereo_difference},
                          {"effect_difference", effect_difference},
                          {"output", relocated.string()},
                          {"scope", "Full exporter/inventory and Linux CPU PCM; synthetic kit, no "
                                    "graphical or platform executable acceptance"}}
                         .dump()
                  << '\n';
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
