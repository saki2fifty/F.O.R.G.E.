#include "asset_reference_impact.hpp"
#include "audio_pcm_fixture.hpp"
#include "audio_publication_fixture.hpp"
#include "audio_test_fixture.hpp"
#include <forge/game_settings.hpp>
#include <forge/project.hpp>

namespace {
AssetId response(const std::filesystem::path& root, std::string name,
                 const forge::asset_detail::AudioClipData& clip) {
    const auto relative = std::filesystem::path("Assets") / name;
    const auto bytes = forge::test::float_wav(clip);
    std::ofstream file(root / relative, std::ios::binary);
    file.write(reinterpret_cast<const char*>(bytes.data()), std::streamsize(bytes.size()));
    file.close();
    return AssetCatalog::register_audio_clip(root, relative).id;
}
AudioRouting graph(AssetId impulse, float wet, bool nested = false) {
    AudioBusEffects fx;
    fx.impulse_response = AudioBusImpulse{{impulse}, wet};
    AudioRouting result{{{"room", "Room", .65f, nested ? "hall" : "master", {}, fx}}};
    if (nested)
        result.buses.push_back({"hall", "Hall", .75f, "master", {}, fx});
    else
        result.buses[0].sends.push_back({"effects", .25f});
    return result;
}
std::vector<float> expected(const std::vector<float>& input,
                            const forge::asset_detail::AudioClipData& impulse, float wet,
                            float gain) {
    std::vector<float> output(input.size());
    for (std::size_t frame = 0; frame < input.size() / 2; ++frame)
        for (unsigned channel = 0; channel < 2; ++channel) {
            double sum = 0;
            for (std::size_t tap = 0; tap <= frame && tap < impulse.frames; ++tap)
                sum += double(impulse.pcm[tap * impulse.channels +
                                          (impulse.channels == 1 ? 0 : channel)]) *
                       input[2 * (frame - tap) + channel];
            output[2 * frame + channel] =
                float(gain * ((1 - wet) * input[2 * frame + channel] + wet * sum));
        }
    return output;
}
void pcm(const std::filesystem::path& root, AssetId tone, AssetId impulse_id,
         const forge::asset_detail::AudioClipData& impulse, bool nested) {
    constexpr float wet = .6f;
    Fixture actual(root, 1, false, graph(impulse_id, wet, nested)), dry(root);
    auto doc = source(tone);
    doc["entities"][0]["components"]["forge.audio_source"]["gain"] = .3;
    dry.load(doc);
    doc["entities"][0]["components"]["forge.audio_source"]["custom_bus"] = "room";
    actual.load(doc);
    check(actual.audio->wait_for_sources(std::chrono::seconds(5)),
          "Valid impulse bus failed preparation");
    actual.simulation.audio_paused(false);
    dry.simulation.audio_paused(false);
    std::vector<float> actual_pcm(2 * 8192), dry_pcm(actual_pcm.size());
    actual.audio->read_offline(actual_pcm);
    dry.audio->read_offline(dry_pcm);
    auto reference = expected(dry_pcm, impulse, wet, .65f);
    if (nested)
        reference = expected(reference, impulse, wet, .75f);
    else
        for (auto& value : reference)
            value *= 1.25f;
    for (std::size_t i = 0; i < reference.size(); ++i)
        if (std::abs(reference[i] - actual_pcm[i]) > 3e-6)
            throw std::runtime_error(
                "Native bus impulse differs from direct PCM oracle sample=" + std::to_string(i) +
                " actual=" + std::to_string(actual_pcm[i]) +
                " expected=" + std::to_string(reference[i]));
    const auto status = actual.audio->status();
    check(status.at("bus_processing") == true &&
              status.at("bus_impulse_buffer_bytes").get<std::size_t>() > 0 &&
              status.at("pending_bus_effects") == false && status.at("bus_effect_error") == "" &&
              status.at("voices") == 1,
          "Impulse processing lost its resource/memory/voice observations");
    actual.audio->game_paused(true);
    // Existing native parent groups retain a bounded buffer at control edges.
    (void)actual.energy(8);
    check(actual.energy() == 0, "Pausable impulse bus kept playing during game pause");
    actual.audio->game_paused(false);
    check(actual.energy() > 0, "Impulse bus failed to resume");
    actual.audio->mix_volumes(0, {1, 1, 1});
    (void)actual.energy(8);
    check(actual.energy() == 0, "Master volume failed to mute processed output");
    actual.audio->mix_volumes(1, {1, 1, 1});
    (void)actual.energy(8);
    check(actual.energy() > 0, "Master unmute lost retained impulse processing");
    actual.audio->stop_source(actual.ref());
    actual.tick();
    check(actual.energy(512) > 1e-8, "Source Stop erased the shared impulse response tail");
    (void)actual.energy(unsigned(2 * impulse.frames + 512));
    check(actual.energy() == 0, "Finite shared impulse tail did not retire to silence");
}
void invalid(const std::filesystem::path& root, AssetId tone, AssetId impulse,
             std::string_view expected = {}, unsigned copies = 1) {
    auto routing = graph(impulse, 1);
    for (unsigned i = 1; i < copies; ++i) {
        auto next = routing.buses[0];
        next.key = "room_" + std::to_string(i);
        routing.buses.push_back(std::move(next));
    }
    Fixture fixture(root, 1, false, routing);
    fixture.load(source(tone));
    check(!fixture.audio->wait_for_sources(std::chrono::seconds(5)),
          "Invalid impulse reported readiness");
    const auto status = fixture.audio->status();
    check(status.at("bus_effect_error") != "" && status.at("voices") == 0,
          "Invalid impulse silently played bypassed sources");
    check(status.at("bus_effect_error").get<std::string>().find(expected) != std::string::npos,
          "Impulse failure did not identify the rejected resource/budget");
    reject([&] { (void)fixture.energy(); });
}
} // namespace
int main(int argc, char** argv) {
    try {
        check(argc == 2, "Scratch path required");
        const auto root = std::filesystem::path(argv[1]) / AssetId::generate().str();
        std::filesystem::create_directories(root / "Assets");
        wav(root / "Assets/tone.wav");
        const auto tone = AssetCatalog::register_audio_clip(root, "Assets/tone.wav").id;
        forge::asset_detail::AudioClipData stereo;
        stereo.channels = 2;
        stereo.frames = 1025;
        stereo.rate = 48000;
        stereo.pcm.resize(stereo.frames * stereo.channels);
        stereo.pcm[0] = .25f;
        stereo.pcm[1] = .5f;
        stereo.pcm[512] = -.125f;
        stereo.pcm[513] = .0625f;
        stereo.pcm[2048] = .125f;
        stereo.pcm[2049] = -.25f;
        const auto impulse = response(root, "stereo-response.wav", stereo);
        auto project = ProjectSettings::defaults("Impulse references");
        project["game"] = default_game_settings("org.forge.impulse", "Impulse response");
        project["game"]["audio"]["routing"] = audio_routing_json(graph(impulse, 0));
        const std::array references{AssetReferenceDocument{"forge.project.json", project}};
        const auto impact = collect_asset_references({{"components", Json::array()}}, references);
        check(impact.uninspected.empty() && impact.references.size() == 1 &&
                  impact.references[0].target == impulse &&
                  impact.references[0].expected_type == AudioClipAsset::type,
              "Project impulse reference escaped asset impact inspection");
        pcm(root, tone, impulse, stereo, false);
        pcm(root, tone, impulse, stereo, true);
        auto mono = stereo;
        mono.channels = 1;
        mono.pcm.assign(stereo.frames, 0);
        mono.pcm[0] = .25f;
        mono.pcm[256] = -.125f;
        mono.pcm[1024] = .125f;
        pcm(root, tone, response(root, "mono-response.wav", mono), mono, false);
        mono.rate = 44100;
        invalid(root, tone, response(root, "wrong-rate.wav", mono));
        mono.rate = 48000;
        mono.frames = 96001;
        mono.pcm.assign(mono.frames, 0);
        invalid(root, tone, response(root, "too-long.wav", mono));
        mono.frames = 1025;
        mono.pcm.assign(mono.frames, 1);
        invalid(root, tone, response(root, "excessive-gain.wav", mono));
        mono.frames = 48000;
        mono.pcm.assign(mono.frames, 0);
        mono.pcm[0] = .5f;
        const auto stream_id = response(root, "stream-response.wav", mono);
        const auto streamed = test::publish_audio_fixture(root, "stream-response", true);
        check(streamed.id == stream_id, "Streaming publication changed response asset identity");
        invalid(root, tone, stream_id, "resident 48000 Hz");
        stereo.frames = 96000;
        stereo.pcm.assign(stereo.frames * 2, 0);
        stereo.pcm[0] = .5f;
        stereo.pcm[1] = .25f;
        const auto large = response(root, "budget-response.wav", stereo);
        invalid(root, tone, large, "32 MiB", 5);
        std::cout << "Native impulse mono/stereo PCM, nested buses/fanout, pause/mute and failed "
                     "asset preparation passed\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
