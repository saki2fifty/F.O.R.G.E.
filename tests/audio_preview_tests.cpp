#include "audio_preview.hpp"
#include <array>
#include <cmath>
#include <forge/scene.hpp>
#include <fstream>
#include <iostream>
#include <limits>
using namespace forge;
namespace {
void require(bool value, const char* message) {
    if (!value)
        throw std::runtime_error(message);
}
template <class F> void reject(F action) {
    bool failed = false;
    try {
        action();
    } catch (const std::exception&) {
        failed = true;
    }
    require(failed, "Invalid preview operation was accepted");
}
std::string bytes(const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::binary);
    require(bool(file), "Cannot read preview fixture");
    return {std::istreambuf_iterator<char>(file), {}};
}
double power(AudioClipPreview& preview) {
    std::array<float, 8192> warm{};
    preview.read_offline(warm);
    std::array<float, 9600> pcm{};
    preview.read_offline(pcm);
    double result = 0;
    for (float value : pcm)
        result += double(value) * value;
    return result / pcm.size();
}
} // namespace
int main(int argc, char** argv) {
    try {
        require(argc == 3, "Need Gallery and private scratch");
        const auto root = std::filesystem::absolute(argv[2]) / AssetId::generate().str();
        std::filesystem::create_directories(root.parent_path());
        std::filesystem::copy(argv[1], root, std::filesystem::copy_options::recursive);
        const auto clip = AssetId::parse("327101c1-e496-4293-b830-4c25b978607a");
        auto catalog = AssetCatalog::open_project(root);
        const auto record = catalog.records().at(clip);
        const auto index = AssetCatalog::project_index(root);
        const auto original_index = bytes(index);
        const auto scene_path = root / "Scenes/33-audio-mixer.scene.json";
        const auto original_scene = bytes(scene_path);
        EngineContext author;
        Scene authored(author.world());
        authored.restore_snapshot(Json::parse(original_scene));
        const auto snapshot = authored.snapshot();
        AudioClipPreview preview;
        preview.configure(true, 1);
        preview.start(root, record, AudioOutput::Offline);
        require(preview.active() && preview.matches(root, record), "Preview did not own selection");
        const auto nominal = power(preview);
        require(nominal > 1e-5, "Preview produced no actual PCM");
        preview.configure(true, .25f);
        require(std::abs(power(preview) / nominal - .0625) < .004,
                "Preview volume did not scale actual power independently");
        preview.configure(true, 0);
        require(power(preview) < 1e-12 && preview.active(),
                "Muted preview stopped or remained audible");
        preview.configure(true, 1);
        require(std::abs(power(preview) / nominal - 1) < .004, "Preview unmute failed");
        preview.stop();
        require(!preview.active(), "Explicit Stop retained preview");
        std::array<float, 512> unused{};
        reject([&] { preview.read_offline(unused); });
        // Real waveform continuity: changing preview controls must not recreate its voice.
        AudioClipPreview reference;
        for (auto* p : {&preview, &reference}) {
            p->configure(true, .5f);
            p->start(root, record, AudioOutput::Offline);
        }
        for (unsigned i = 0; i < 12; ++i) {
            preview.configure(true, .5f);
            std::array<float, 512> expected{}, actual{};
            reference.read_offline(expected);
            preview.read_offline(actual);
            require(actual == expected, "Live preview controls shifted the sample cursor");
        }
        for (float invalid : {-1.f, 2.f, std::numeric_limits<float>::quiet_NaN()}) {
            reject([&] { preview.configure(false, invalid); });
            std::array<float, 512> expected{}, actual{};
            reference.read_offline(expected);
            preview.read_offline(actual);
            require(actual == expected, "Invalid preview control partially changed playback");
        }
        reference.stop();
        preview.configure(false, .5f);
        std::vector<float> two_seconds(192000);
        preview.read_offline(two_seconds);
        preview.poll();
        require(!preview.active(), "Natural clip completion retained device/cache");
        preview.configure(true, .5f);
        preview.start(root, record, AudioOutput::Offline);
        preview.read_offline(two_seconds);
        preview.poll();
        require(preview.active(), "Loop preview ended at clip duration");
        preview.start(root, record, AudioOutput::Offline);
        std::array<float, 512> restarted{};
        preview.read_offline(restarted);
        reference.start(root, record, AudioOutput::Offline);
        reference.read_offline(unused);
        require(restarted == unused, "Restart preview did not restart from frame zero");
        reference.stop();
        auto stale = record;
        stale.metadata["forge.audio"]["duration"] = 2.0;
        require(!preview.matches(root, stale) && !preview.matches(root.parent_path(), record),
                "Preview selection ignored revision or project change");
        reject([&] { preview.start(root, stale, AudioOutput::Offline); });
        require(!preview.active(), "Failed replacement retained old audible preview");
        auto bad = record;
        bad.type = "texture";
        reject([&] { preview.start(root, bad, AudioOutput::Offline); });
        bad = record;
        bad.id = AssetId::generate();
        reject([&] { preview.start(root, bad, AudioOutput::Offline); });
        bad = record;
        bad.metadata["forge.import"]["artifact_digest"] = std::string(64, '0');
        catalog.replace(bad);
        catalog.save(index);
        reject([&] { preview.start(root, bad, AudioOutput::Offline); });
        require(!preview.active(), "Corrupt cooked revision created preview");
        {
            std::ofstream restore(index, std::ios::binary);
            restore << original_index;
        }
        // Audition uses published immutable PCM, including when the source is absent.
        const auto wave = root / record.source;
        const auto held = root / "held-tone.wav";
        std::filesystem::rename(wave, held);
        preview.start(root, record, AudioOutput::Offline);
        require(power(preview) > 1e-5, "Published cooked preview depended on source WAV");
        preview.stop();
        std::filesystem::rename(held, wave);
        for (unsigned i = 0; i < 3; ++i) {
            preview.start(root, record, AudioOutput::NullDeviceTest);
            preview.stop();
        }
        require(bytes(index) == original_index && bytes(scene_path) == original_scene &&
                    authored.snapshot() == snapshot &&
                    !author.services().available(Capability::Audio),
                "Editor audition mutated authored content or opened authoring audio");
        std::filesystem::remove_all(root);
        std::cout << "Audio preview PCM/revision/authoring isolation and device lifecycle passed\n";
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
