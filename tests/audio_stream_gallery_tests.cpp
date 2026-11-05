#include "asset_bytes.hpp"
#include "runtime_package.hpp"
#include <array>
#include <cmath>
#include <forge/audio.hpp>
#include <forge/runtime.hpp>
#include <fstream>
#include <iostream>
using namespace forge;
namespace {
void require(bool value, const char* why) {
    if (!value)
        throw std::runtime_error(why);
}
struct Game {
    EngineContext engine;
    Scene scene;
    RuntimeSimulation simulation;
    std::shared_ptr<AudioRuntime> audio;
    Game(const std::filesystem::path& root, const Json& document)
        : engine(WorldRole::Runtime, false, {audio_module({root, AudioOutput::Offline, true})}),
          scene(engine.world()), simulation(engine.world(), scene),
          audio(std::static_pointer_cast<AudioRuntime>(engine.services().audio())) {
        scene.restore_snapshot(document);
        simulation.reset_presentation();
        simulation.sync_audio();
        require(audio->wait_for_sources(std::chrono::seconds(5)), "Gallery audio did not prepare");
        simulation.audio_paused(false);
    }
};
Json read(const std::filesystem::path& path) {
    std::ifstream in(path);
    require(bool(in), "Gallery Scene missing");
    return Json::parse(in);
}
} // namespace
int main(int argc, char** argv) {
    try {
        require(argc == 3, "Need Gallery and private scratch");
        const auto scratch = std::filesystem::absolute(argv[2]) / AssetId::generate().str();
        const auto source = scratch / "source", original = scratch / "package";
        std::filesystem::create_directories(scratch);
        std::filesystem::copy(argv[1], source, std::filesystem::copy_options::recursive);
        auto catalog = AssetCatalog::open_project(source);
        AssetId scene_id, clip_id;
        for (const auto& [id, record] : catalog.records()) {
            if (record.source == "Scenes/172-audio-streaming.scene.json")
                scene_id = id;
            if (record.source == "Assets/AudioStreaming/PagePulse.wav")
                clip_id = id;
        }
        require(bool(scene_id) && bool(clip_id), "Streaming Gallery identities missing");
        const auto metadata = catalog.records().at(clip_id).metadata.at("forge.audio");
        require(metadata.at("playback_storage") == "stream" &&
                    metadata.at("sample_rate") == 48000 && metadata.at("channels") == 2 &&
                    metadata.at("frames") == 576000,
                "Gallery stream profile differs from source");
        const std::array roots{scene_id};
        {
            ProjectLease lease(source);
            prepare_runtime_content_catalog(lease, roots, {});
        }
        const RuntimePackageTarget target{"windows", "d3d12"};
        package_runtime_content(source, original, roots, target);
        const auto relocated = scratch / "relocated";
        std::filesystem::rename(original, relocated);
        const auto packaged = open_runtime_content(relocated, target);
        require(packaged.records().contains(clip_id), "Export closure omitted streamed clip");
        for (const auto& entry : std::filesystem::recursive_directory_iterator(relocated))
            require(entry.path().extension() != ".wav" && entry.path().filename() != "audio.wav",
                    "Export retained development WAV or temporary backing");
        const auto source_doc = read(source / catalog.records().at(scene_id).source);
        const auto game_doc = read(relocated / packaged.records().at(scene_id).source);
        Game authored(source, source_doc), exported(relocated, game_doc);
        std::filesystem::rename(source, scratch / "source-hidden");
        double energy = 0;
        std::array<float, 512> a{}, b{};
        for (unsigned quantum = 0; quantum < 2600; ++quantum) {
            authored.audio->read_offline(a);
            exported.audio->read_offline(b);
            require(a == b, "Relocated source-hidden stream PCM differs from authored Game");
            for (float v : b) {
                require(std::isfinite(v), "Export stream produced nonfinite PCM");
                energy += double(v) * v;
            }
        }
        require(energy > 1 && exported.audio->status().at("decoded_bytes") == 0 &&
                    exported.audio->status().at("stream_underruns") == 0 &&
                    exported.audio->status().at("stream_backing_files") == 1,
                "Export stream was silent, resident or starved");
        // Fresh runtime reconstruction uses packaged values and ordinary loading,
        // without a source WAV, copied provider handles or recovered cursor.
        Game recovered(relocated, game_doc);
        recovered.audio->read_offline(a);
        Game fresh(relocated, game_doc);
        fresh.audio->read_offline(b);
        require(a == b, "Reconstructed stream did not restart from saved configuration");
        std::cout << "Gallery172 source-free relocated stream closure and 13.8s loop PCM passed\n";
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
