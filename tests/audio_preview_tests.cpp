#include "audio_loading_test.hpp"
#include "audio_preview.hpp"
#include <algorithm>
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
void waveform_checks() {
    asset_detail::AudioClipData clip{{-.7f, .2f, .9f, -.4f, .3f, .8f}, 3, 2, 24000};
    auto waveform = asset_detail::audio_waveform(clip);
    require(waveform.buckets() == 3 && waveform.channels == 2 && waveform.rate == 24000 &&
                waveform.duration() == 3. / 24000 && waveform.peaks[0][0] == -.7f &&
                waveform.peaks[3][1] == -.4f && waveform.peaks[5][1] == .8f,
            "Short stereo envelope lost sample/channel identity");
    clip = {std::vector<float>(8193, 0), 8193, 1, 48000};
    clip.pcm[0] = -.8f;
    clip.pcm[1] = .5f;
    clip.pcm.back() = 1.2f;
    waveform = asset_detail::audio_waveform(clip);
    require(waveform.buckets() == 4096 && waveform.peaks[0] == std::array<float, 2>{-.8f, .5f} &&
                waveform.peaks.back()[1] == 1.2f,
            "Envelope dropped extrema or nondivisible tail");
    clip.pcm.back() = std::numeric_limits<float>::quiet_NaN();
    reject([&] { asset_detail::audio_waveform(clip); });
}
void seek_checks(const std::filesystem::path& root, unsigned rate) {
    const auto project = root / ("seek-" + std::to_string(rate));
    std::filesystem::create_directories(project / "Assets");
    std::ofstream out(project / "Assets/seek.wav", std::ios::binary);
    auto integer = [&](std::uint32_t value, unsigned n) {
        for (unsigned i = 0; i < n; ++i)
            out.put(char(value >> (8 * i)));
    };
    out.write("RIFF", 4);
    integer(36 + rate * 2, 4);
    out.write("WAVEfmt ", 8);
    integer(16, 4);
    integer(1, 2);
    integer(1, 2);
    integer(rate, 4);
    integer(rate * 2, 4);
    integer(2, 2);
    integer(16, 2);
    out.write("data", 4);
    integer(rate * 2, 4);
    for (unsigned i = 0; i < rate; ++i)
        integer(std::uint16_t(std::int16_t(i < rate / 2 ? 8192 : -16384)), 2);
    out.close();
    auto record = AssetCatalog::register_audio_clip(project, "Assets/seek.wav");
    AudioClipPreview preview, reference;
    preview.configure(true, 1);
    reference.configure(true, 1);
    preview.load_waveform(project, record);
    await_audio_preview(preview);
    require(!preview.active() && preview.waveform_matches(project, record) &&
                preview.waveform()->peaks.front()[1] == .25f &&
                preview.waveform()->peaks.back()[0] == -.5f,
            "Loading waveform opened playback or changed PCM extrema");
    preview.start(project, record, AudioOutput::Offline);
    await_audio_preview(preview);
    reference.start(project, record, AudioOutput::Offline);
    await_audio_preview(reference);
    std::array<float, 2048> a{}, b{};
    preview.read_offline(a);
    reference.read_offline(b);
    preview.paused(true);
    const auto held = preview.position();
    preview.read_offline(a);
    require(preview.paused() && preview.position() == held &&
                std::all_of(a.begin(), a.end(), [](float x) { return x == 0; }),
            "Pause advanced preview time or output PCM");
    preview.paused(false);
    preview.read_offline(a);
    reference.read_offline(b);
    require(a == b, "Pause/resume lost sample continuity");
    preview.seek(.75);
    require(std::abs(preview.position() - .75) < 1. / rate, "Seek position used wrong sample rate");
    preview.read_offline(a);
    require(std::abs(preview.position() - (.75 + 1024. / 48000)) < 2. / rate && a.back() < -.3f,
            "Seek retained old cached PCM or advanced time at source rate");
    preview.paused(true);
    preview.seek(.25);
    require(preview.paused() && std::abs(preview.position() - .25) < 1. / rate,
            "Seeking resumed a paused preview");
    for (double value : {-1., 2., std::numeric_limits<double>::quiet_NaN(),
                         std::numeric_limits<double>::infinity()}) {
        reject([&] { preview.seek(value); });
        require(preview.active() && preview.paused() &&
                    std::abs(preview.position() - .25) < 1. / rate,
                "Invalid seek changed admitted preview state");
    }
    preview.paused(false);
    preview.read_offline(a);
    require(a.back() > .15f, "Seek backward did not discard stale resampler state");
    preview.seek(.99);
    preview.read_offline(a);
    require(preview.position() < .02 && a.back() > .15f,
            "Seek-to-loop wrap used stale time or PCM");
    preview.configure(false, 1);
    preview.seek(1);
    preview.read_offline(a);
    preview.poll();
    require(!preview.active() && preview.waveform(), "End seek retained sound or dropped envelope");
    preview.clear();
    reference.clear();
    require(!preview.waveform(), "Preview close retained waveform selection");
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
        waveform_checks();
        seek_checks(root, 48000);
        seek_checks(root, 24000);
        AudioClipPreview preview;
        {
            AudioClipPreview cancelled;
            cancelled.start(root, record, AudioOutput::Offline);
            require(cancelled.pending() && cancelled.waveform_pending(),
                    "Fresh audition did not expose its pending clip/envelope");
            cancelled.stop();
            require(!cancelled.active() && !cancelled.pending() && !cancelled.waveform_pending() &&
                        !cancelled.waveform(),
                    "Stop retained a pending audition or envelope job");
        }
        preview.configure(true, 1);
        preview.start(root, record, AudioOutput::Offline);
        await_audio_preview(preview);
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
            await_audio_preview(*p);
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
        await_audio_preview(preview);
        preview.read_offline(two_seconds);
        preview.poll();
        require(preview.active(), "Loop preview ended at clip duration");
        preview.start(root, record, AudioOutput::Offline);
        await_audio_preview(preview);
        std::array<float, 512> restarted{};
        preview.read_offline(restarted);
        reference.start(root, record, AudioOutput::Offline);
        await_audio_preview(reference);
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
        reject([&] {
            preview.start(root, bad, AudioOutput::Offline);
            await_audio_preview(preview);
        });
        bad = record;
        bad.id = AssetId::generate();
        reject([&] {
            preview.start(root, bad, AudioOutput::Offline);
            await_audio_preview(preview);
        });
        bad = record;
        bad.metadata["forge.import"]["artifact_digest"] = std::string(64, '0');
        catalog.replace(bad);
        catalog.save(index);
        reject([&] {
            preview.start(root, bad, AudioOutput::Offline);
            await_audio_preview(preview);
        });
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
        await_audio_preview(preview);
        require(power(preview) > 1e-5, "Published cooked preview depended on source WAV");
        preview.stop();
        std::filesystem::rename(held, wave);
        for (unsigned i = 0; i < 3; ++i) {
            preview.start(root, record, AudioOutput::NullDeviceTest);
            await_audio_preview(preview);
            for (unsigned seek = 0; seek < 12; ++seek) {
                preview.paused(seek % 2);
                preview.seek((seek % 3) * .2);
                require(std::isfinite(preview.position()), "Device preview time became nonfinite");
            }
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
