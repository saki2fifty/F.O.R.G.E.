#include "audio_source_access.hpp"
#include "runtime_package.hpp"
#include "transform_animation_authoring.hpp"
#include "transform_animation_preview.hpp"
#include <forge/animation.hpp>
#include <forge/audio.hpp>
#include <forge/authoring.hpp>
#include <forge/render_scene.hpp>
#include <forge/runtime.hpp>
#include <fstream>
#include <iostream>
#include <limits>
#include <numbers>
#include <thread>
using namespace forge;
namespace {
void check(bool value, const char* message) {
    if (!value)
        throw std::runtime_error(message);
}
void near(double actual, double expected) {
    check(std::abs(actual - expected) < .00005, "Animated audio value differs");
}
template <class F> void reject(F action) {
    try {
        action();
    } catch (const std::exception&) {
        return;
    }
    throw std::runtime_error("Invalid animated audio input accepted");
}
void tone(const std::filesystem::path& file) {
    constexpr unsigned rate = 48000;
    std::ofstream stream(file, std::ios::binary);
    const auto integer = [&](unsigned value, unsigned bytes) {
        for (unsigned byte = 0; byte < bytes; ++byte)
            stream.put(char(value >> (byte * 8)));
    };
    stream.write("RIFF", 4);
    integer(36 + 2 * rate, 4);
    stream.write("WAVEfmt ", 8);
    integer(16, 4);
    integer(1, 2);
    integer(1, 2);
    integer(rate, 4);
    integer(2 * rate, 4);
    integer(2, 2);
    integer(16, 2);
    stream.write("data", 4);
    integer(2 * rate, 4);
    for (unsigned frame = 0; frame < rate; ++frame) {
        // A silent lead-in makes an accidental voice restart observable.
        const auto value =
            frame < rate / 4
                ? 0
                : std::int16_t(6000 * std::sin(2 * std::numbers::pi * 440 * frame / rate));
        integer(std::uint16_t(value), 2);
    }
    check(bool(stream), "Could not write original fixture tone");
}
struct Signal {
    double energy, frequency;
};
struct Running {
    EngineContext engine;
    Scene scene;
    RuntimeSimulation simulation;
    std::shared_ptr<AnimationRuntime> animation;
    std::shared_ptr<AudioRuntime> audio;
    explicit Running(const std::filesystem::path& root)
        : engine(WorldRole::Runtime, false,
                 {animation_module(root), audio_module({root, AudioOutput::Offline, true})}),
          scene(engine.world()), simulation(engine.world(), scene),
          animation(animation_runtime(engine.world())),
          audio(std::static_pointer_cast<AudioRuntime>(engine.services().audio())) {}
    void load(const Json& source, std::shared_ptr<const AssetCatalog> catalog) {
        animation->catalog(std::move(catalog));
        scene.restore_snapshot(source);
    }
    void ready() {
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
        while (!animation->prepare_initial_pose()) {
            check(std::chrono::steady_clock::now() < deadline, "Animated audio admission stalled");
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        simulation.reset_presentation();
        simulation.sync_audio();
        check(audio->wait_for_sources(std::chrono::seconds(5)), "Animated audio load failed");
        simulation.audio_paused(false);
    }
    Signal signal(unsigned frames = 4096) {
        std::vector<float> pcm(2 * frames);
        audio->read_offline(pcm);
        double energy = 0;
        unsigned rising = 0;
        for (unsigned frame = 0; frame < frames; ++frame) {
            check(std::isfinite(pcm[2 * frame]) && std::isfinite(pcm[2 * frame + 1]),
                  "Animated audio produced invalid PCM");
            energy += double(pcm[2 * frame]) * pcm[2 * frame];
            if (frame && pcm[2 * frame] > 0 && pcm[2 * frame - 2] <= 0)
                ++rising;
        }
        return {energy / frames, double(rising) * 48000 / frames};
    }
};
Json read(const std::filesystem::path& file) {
    Json result;
    std::ifstream(file) >> result;
    return result;
}
void exercise(const std::filesystem::path& root) {
    std::filesystem::create_directories(root / "Assets");
    tone(root / "Assets/OriginalTone.wav");
    const auto sound = AssetCatalog::register_audio_clip(root, "Assets/OriginalTone.wav");
    auto lease = std::make_shared<ProjectLease>(root);
    auto document = TransformAnimationDocument::create(lease, "Assets/Audio.animation.json");
    const auto clip_id = document->source().asset();
    const auto original = document->source().document;
    auto source = document->source();
    source.add_property(AnimationProperty::AudioGain, .25);
    source.add_property(AnimationProperty::AudioPitch, 1);
    source.document["channels"] = Json::array();
    source.document["keys"][0]["extension"] = "retain";
    auto end = source.clip().keys.front().properties;
    end[unsigned(AnimationProperty::AudioGain)] = 1;
    end[unsigned(AnimationProperty::AudioPitch)] = 2;
    source.set_key(animation_timebase, {}, false, {}, {}, end);
    source.set_key(2 * animation_timebase, {}, false, {}, {},
                   source.clip().keys.front().properties);
    check(source.clip().channels == 0 && source.clip().properties == 192,
          "Audio-only clip changed transform/property authority");
    const auto half = source.clip().sample_properties(500000);
    near(half[unsigned(AnimationProperty::AudioGain)], .625);
    near(half[unsigned(AnimationProperty::AudioPitch)], 1.5);
    auto held = source;
    held.document["keys"][0]["interpolation"] = "held";
    near(held.clip().sample_properties(999999)[unsigned(AnimationProperty::AudioGain)], .25);
    near(held.clip().sample_properties(1000000)[unsigned(AnimationProperty::AudioPitch)], 2);
    for (const auto [property, invalid] :
         {std::pair{AnimationProperty::AudioGain, -1.},
          std::pair{AnimationProperty::AudioGain, 4.001},
          std::pair{AnimationProperty::AudioPitch, .249},
          std::pair{AnimationProperty::AudioPitch, 4.001},
          std::pair{AnimationProperty::AudioPitch, std::numeric_limits<double>::quiet_NaN()}}) {
        auto values = end;
        values[unsigned(property)] = invalid;
        const auto before = source.document;
        reject([&] { source.set_key(500000, {}, false, {}, {}, values); });
        check(source.document == before, "Rejected audio key changed source/history");
    }
    auto retimed = source;
    retimed.retime_duration(4 * animation_timebase);
    check(retimed.clip().keys[1].properties == end &&
              retimed.document["keys"][0]["extension"] == "retain",
          "Audio retiming lost values/extensions");
    document->edit(document->revision(), "Animate audio",
                   [&](Json& value) { value = source.document; });
    document->undo();
    check(document->source().document == original, "Audio source Undo failed");
    document->redo();
    check(document->source().document == source.document, "Audio source Redo failed");
    document->save();
    AssetImportService importer(lease, transform_animation_import_registry(),
                                {"windows", "d3d12", "desktop"});
    importer.submit(
        importer.prepare(document->locator(), {}, clip_id),
        [](auto& candidate, const auto& plan, const auto&) {
            prepare_transform_animation_publication(candidate, plan);
        },
        [](const auto&, const auto&) {});
    check(importer.wait_idle(std::chrono::seconds(10)), "Audio animation publication stalled");
    const auto receipts = importer.poll();
    check(receipts.size() == 1 && receipts.front().published && receipts.front().publication,
          "Audio animation was not published");
    const auto catalog =
        std::make_shared<const AssetCatalog>(receipts.front().publication->catalog);
    check(catalog->records().at(clip_id).metadata["forge.import"]["importer_revision"] ==
              "forge.animation.transform.v5",
          "Expanded property semantics reused the old recipe revision");

    EngineContext author;
    Scene authored(author.world());
    const auto speaker = authoring_command(authored, "entity.create", {{"name", "Speaker"}})
                             .at("selected")
                             .get<std::string>();
    authoring_command(authored, "component.add",
                      {{"entity", speaker}, {"component", "forge.audio_source"}});
    authoring_command(authored, "component.add",
                      {{"entity", speaker}, {"component", "forge.transform_animator"}});
    auto sound_config = authored.entity(speaker).get<AudioSource>();
    sound_config.clip = {sound.id};
    sound_config.play_on_start = sound_config.loop = true;
    sound_config.spatialized = false;
    authored.entity(speaker).set<AudioSource>(sound_config);
    auto animator = authored.entity(speaker).get<TransformAnimator>();
    animator.clip = {clip_id};
    authored.entity(speaker).set<TransformAnimator>(animator);
    const auto saved = authored.snapshot();
    auto preview = saved;
    check(apply_transform_animation_preview(
              preview, {authored.asset_id(), EntityId::parse(speaker)}, source.clip(), 500000),
          "Audio preview target unavailable");
    near(preview["entities"][0]["components"]["forge.audio_source"]["gain"].get<double>(), .625);
    check(authored.snapshot() == saved, "Audio preview changed authored data");
    for (bool missing : {true, false}) {
        auto bad = saved;
        auto& components = bad["entities"][0]["components"];
        if (missing)
            components.erase("forge.audio_source");
        else
            components["forge.audio_source"]["maximum_distance"] = .5;
        const auto before = bad;
        reject([&] {
            apply_transform_animation_preview(bad, {authored.asset_id(), EntityId::parse(speaker)},
                                              source.clip(), 500000);
        });
        check(bad == before, "Rejected audio preview wrote a partial candidate");
    }

    Running live(root);
    live.load(saved, catalog);
    live.ready();
    const auto unbound = live.scene.entity(speaker).get<AudioSource>();
    near(unbound.gain, .25);
    near(unbound.pitch, 1);
    (void)live.signal(14400);
    const auto initial = live.signal();
    check(initial.energy > .0001 && std::abs(initial.frequency - 440) < 15,
          "Original audio fixture did not reach the backend voice");
    live.simulation.tick(.5f);
    const auto animated = live.scene.entity(speaker).get<AudioSource>();
    near(animated.gain, .625);
    near(animated.pitch, 1.5);
    auto retained = animated;
    retained.gain = unbound.gain;
    retained.pitch = unbound.pitch;
    check(retained == unbound, "Audio animation altered unbound fields");
    const auto changed = live.signal();
    check(changed.energy / initial.energy > 5.8 && changed.energy / initial.energy < 6.7 &&
              std::abs(changed.frequency - 660) < 15,
          "Gain/pitch did not reach retained PCM voice or restarted the silent intro");
    const auto checkpoint = live.animation->checkpoint();
    const auto before_reads = live.scene.snapshot();
    (void)live.simulation.presentation(.5);
    (void)AudioSourceAccess::status(
        *live.audio, live.engine.world().reference(live.scene.entity(speaker).id()).value());
    check(live.scene.snapshot() == before_reads && live.animation->checkpoint() == checkpoint,
          "Audio presentation/status advanced playback or changed semantic values");
    live.audio->paused(true);
    check(live.signal().energy == 0 && live.animation->checkpoint() == checkpoint,
          "Paused audio generated sound or advanced animation");
    live.audio->paused(false);
    check(live.signal().energy > .001, "Animated voice did not resume");
    Running recovered(root);
    recovered.load(saved, catalog);
    recovered.ready();
    recovered.animation->restore(checkpoint);
    recovered.simulation.sync_audio();
    near(recovered.scene.entity(speaker).get<AudioSource>().gain, .625);
    near(recovered.scene.entity(speaker).get<AudioSource>().pitch, 1.5);
    (void)recovered.signal(14400);
    check(std::abs(recovered.signal().frequency - 660) < 15,
          "Recovered semantic audio automation failed");
    animator.enabled = false;
    live.scene.entity(speaker).set<TransformAnimator>(animator);
    live.simulation.tick(.25f);
    check(live.scene.entity(speaker).get<AudioSource>() == animated,
          "Disabled automation changed the retained source values");
    auto two = saved;
    auto second = two["entities"][0];
    second["id"] = EntityId::generate().str();
    second["name"] = "Invalid second source";
    const auto second_id = second["id"].get<std::string>();
    two["entities"].push_back(second);
    Running invalid(root);
    invalid.load(two, catalog);
    auto invalid_source = invalid.scene.entity(second_id).get<AudioSource>();
    invalid_source.outer_angle = 1;
    invalid.scene.entity(second_id).set<AudioSource>(invalid_source);
    reject([&] { invalid.ready(); });
    check(invalid.scene.entity(speaker).get<AudioSource>() == sound_config,
          "Another invalid player allowed a partial audio candidate commit");
    check(read(root / "Assets/Audio.animation.json") == source.document &&
              authored.snapshot() == saved &&
              AssetCatalog::open_project(root).document() == catalog->document(),
          "Runtime audio animation rewrote saved content/catalog");
    std::cout << "Animated gain energy ratio=" << changed.energy / initial.energy
              << ", pitch frequency=" << changed.frequency << '\n';
}
void gallery_exercise(const std::filesystem::path& root, const Json& source,
                      const std::filesystem::path& source_path) {
    const auto catalog = std::make_shared<const AssetCatalog>(AssetCatalog::open_project(root));
    const auto before_catalog = catalog->document();
    const auto before_source = read(root / source_path);
    Running run(root);
    run.load(source, catalog);
    run.ready();
    std::string emitter;
    for (const auto& row : source.at("entities"))
        if (row.at("components").contains("forge.audio_source")) {
            check(emitter.empty(), "Gallery53 has more than one audio source");
            emitter = row.at("id").get<std::string>();
        }
    check(!emitter.empty(), "Gallery53 has no audio source");
    double energy = 0;
    const auto fraction = [](double time) {
        time = std::fmod(time, 6.);
        return time <= 3 ? time / 3 : (6 - time) / 3;
    };
    for (unsigned step = 0; step != 30; ++step) {
        const auto expected = fraction(step * .25);
        const auto config = run.scene.entity(emitter).get<AudioSource>();
        near(config.gain, .1 + .7 * expected);
        near(config.pitch, .5 + expected);
        near(run.scene.entity(emitter).get<Light>().intensity, .25 + 3.25 * expected);
        const auto checkpoint = run.animation->checkpoint();
        const auto render = extract_render_scene(run.simulation.presentation(.5));
        check(render.meshes.size() == 3 && render.cameras.size() == 1 &&
                  render.lights.size() == 1 && render.diagnostics.empty(),
              "Gallery53 lost its camera, light or visible geometry");
        near(render.lights.front().light.intensity,
             .25 + 3.25 * fraction(std::max(0., step * .25 - .125)));
        check(run.animation->checkpoint() == checkpoint &&
                  run.scene.entity(emitter).get<AudioSource>() == config,
              "Gallery53 presentation advanced or changed sound");
        energy += run.signal().energy;
        run.simulation.tick(.25f);
    }
    check(energy > .0001 && run.audio->status().at("voices") == 1,
          "Gallery53 did not render sound through its retained voice");
    check(read(root / source_path) == before_source &&
              AssetCatalog::open_project(root).document() == before_catalog,
          "Gallery53 playback rewrote saved content/catalog");
    std::cout << "Gallery53 offline energy=" << energy << '\n';
}
void gallery(const std::filesystem::path& original, const std::filesystem::path& scratch) {
    const auto root = std::filesystem::absolute(scratch) / AssetId::generate().str();
    std::filesystem::create_directories(root);
    std::filesystem::copy(original, root, std::filesystem::copy_options::recursive);
    const auto path = std::filesystem::path("Scenes/53-animated-audio.scene.json");
    const auto source = read(root / path);
    gallery_exercise(root, source, path);
    const auto game = root.string() + "-game", relocated = game + "-relocated";
    const std::array roots{source.at("asset_id").get<AssetId>()};
    (void)package_runtime_content(root, game, roots, {"windows", "d3d12"});
    std::filesystem::rename(game, relocated);
    std::filesystem::rename(root, root.string() + "-source-offline");
    const auto catalog = open_runtime_content(relocated, {"windows", "d3d12"});
    check(catalog.records().size() == 3, "Gallery53 export retained unrelated records");
    const auto scene = catalog.resolve(AssetRef<SceneAsset>{roots.front()});
    check(scene.state == AssetState::Available, "Relocated Gallery53 scene is unavailable");
    gallery_exercise(relocated, read(std::filesystem::path(relocated) / scene.record->source),
                     scene.record->source);
}
} // namespace
int main(int argc, char** argv) {
    try {
        if (argc == 4 && std::string_view(argv[1]) == "--gallery") {
            gallery(argv[2], argv[3]);
            return 0;
        }
        check(argc == 2, "Need animated audio scratch root");
        exercise(std::filesystem::absolute(argv[1]) / AssetId::generate().str());
        std::cout << "Animated audio tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
