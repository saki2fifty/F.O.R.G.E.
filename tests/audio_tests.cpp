#include <cmath>
#include <forge/assets.hpp>
#include <forge/audio.hpp>
#include <forge/authoring.hpp>
#include <forge/prefab_authoring.hpp>
#include <forge/runtime.hpp>
#include <fstream>
#include <iostream>
#include <thread>
using namespace forge;
void check(bool b, const char* text) {
    if (!b)
        throw std::runtime_error(text);
}
template <class F> void reject(F f) {
    try {
        f();
    } catch (const std::exception&) {
        return;
    }
    throw std::runtime_error("Expected rejection");
}
void wav(const std::filesystem::path& path, unsigned channels = 1, unsigned rate = 48000) {
    std::ofstream out(path, std::ios::binary);
    auto u = [&](std::uint32_t n, unsigned bytes) {
        for (unsigned i = 0; i < bytes; i++)
            out.put(char(n >> (8 * i)));
    };
    out.write("RIFF", 4);
    u(36 + rate * channels * 2, 4);
    out.write("WAVEfmt ", 8);
    u(16, 4);
    u(1, 2);
    u(channels, 2);
    u(rate, 4);
    u(rate * channels * 2, 4);
    u(channels * 2, 2);
    u(16, 2);
    out.write("data", 4);
    u(rate * channels * 2, 4);
    for (unsigned i = 0; i < rate; i++)
        for (unsigned channel = 0; channel < channels; channel++)
            u(std::uint16_t(std::int16_t((channel ? -1 : 1) * 3000 *
                                         std::sin(i * 2 * 3.141592653589793 * 440 / rate))),
              2);
}
Json source(AssetId clip) {
    Json components = {{"forge.position", {{"x", 0}, {"y", 0}, {"z", 0}}},
                       {"forge.audio_source",
                        {{"clip", clip},
                         {"play_on_start", true},
                         {"loop", true},
                         {"gain", 1},
                         {"pitch", 1},
                         {"spatialized", false},
                         {"minimum_distance", 1},
                         {"maximum_distance", 100}}}};
    Json entity = {{"id", "speaker"}, {"name", "Speaker"}, {"components", components}};
    return {{"version", 1}, {"entities", Json::array({entity})}};
}
struct Fixture {
    Module module;
    EngineContext engine;
    Scene scene;
    RuntimeSimulation simulation;
    std::shared_ptr<AudioRuntime> audio;
    explicit Fixture(const std::filesystem::path& root, float volume = 1)
        : engine(WorldRole::Runtime, false,
                 {audio_module({root, AudioOutput::Offline, true, volume})}),
          scene(engine.world()), simulation(engine.world(), scene, module),
          audio(std::static_pointer_cast<AudioRuntime>(engine.services().audio())) {}
    void load(const Json& doc) {
        scene.restore_snapshot(doc);
        simulation.reset_presentation();
        simulation.sync_audio();
    }
    double energy(unsigned frames = 1024) {
        std::vector<float> pcm(frames * 2);
        audio->read_offline(pcm);
        double sum = 0;
        for (auto v : pcm) {
            check(std::isfinite(v), "Invalid PCM");
            sum += double(v) * v;
        }
        return sum / pcm.size();
    }
    EntityRef ref(const char* id = "speaker") {
        return engine.world().reference(scene.entity(id).id()).value();
    }
    void tick() { simulation.tick(1.f / 60); }
};
int main(int argc, char** argv) {
    try {
        check(argc == 2, "Test path required");
        const auto root = std::filesystem::path(argv[1]) / AssetId::generate().str();
        std::filesystem::create_directories(root / "Assets");
        wav(root / "Assets/tone.wav");
        auto record = AssetCatalog::register_audio_clip(root, "Assets/tone.wav");
        check(AssetCatalog::register_audio_clip(root, "Assets/tone.wav").id == record.id,
              "Registration changed identity");
        auto catalog = AssetCatalog::open_project(root);
        {
            Fixture full(root), quiet(root, .5f), muted(root, 0);
            full.load(source(record.id));
            quiet.load(source(record.id));
            muted.load(source(record.id));
            full.simulation.audio_paused(false);
            quiet.simulation.audio_paused(false);
            muted.simulation.audio_paused(false);
            const auto energy = full.energy();
            check(energy > 0 && std::abs(quiet.energy() / energy - .25) < .01 &&
                      muted.energy() == 0,
                  "Master volume did not affect actual offline samples");
            reject([&] { Fixture invalid(root, -1); });
            full.audio->master_volume(0);
            check(full.energy() == 0 && full.audio->status().at("master_volume") == 0,
                  "Runtime master volume did not mute actual samples");
            full.audio->master_volume(1);
            check(full.energy() > 0, "Runtime master volume did not restore audio");
            reject([&] { full.audio->master_volume(-1); });
        }
        check(catalog.resolve(AssetRef<AudioClipAsset>{record.id}).state == AssetState::Available,
              "Typed AudioClip resolution");
        check(catalog.resolve(record.id, SceneAsset::type).state == AssetState::Incompatible,
              "Wrong type accepted");
        reject([&] { AssetCatalog::register_audio_clip(root, "../escape.wav"); });
        std::filesystem::rename(root / "Assets/tone.wav", root / "Assets/moved.wav");
        catalog.relocate(record.id, "Assets/moved.wav");
        catalog.save(AssetCatalog::project_index(root));
        check(AssetCatalog::open_project(root).resolve(record.id, AudioClipAsset::type).state ==
                  AssetState::Available,
              "Move lost AssetId");
        {
            EngineContext authoring;
            WorldContext validation(WorldRole::Validation);
            check(!authoring.services().available(Capability::Audio), "Authoring opened audio");
            check(validation.world().lookup("forge.audio_source"), "Validation schema absent");
        }
        auto bad_record =
            AssetRecord{AssetId::generate(), AudioClipAsset::type, "Assets/bad.wav", 1, {}};
        std::ofstream(root / "Assets/bad.wav") << "not a wave file at all";
        catalog.add(bad_record);
        auto missing =
            AssetRecord{AssetId::generate(), AudioClipAsset::type, "Assets/missing.wav", 1, {}};
        catalog.add(missing);
        auto wrong_type = AssetRecord{AssetId::generate(), "custom", "Assets/other.bin", 1, {}};
        std::ofstream(root / "Assets/other.bin") << "data";
        catalog.add(wrong_type);
        catalog.save(AssetCatalog::project_index(root));
        for (auto id : {bad_record.id, missing.id, wrong_type.id}) {
            Fixture invalid(root);
            invalid.load(source(id));
            check(invalid.audio->status()["voices"] == 0 &&
                      invalid.audio->status()["failed_sources"] == 1,
                  "Invalid clip did not diagnose");
            const auto messages = invalid.engine.services().diagnostics();
            check(!messages.empty(), "Missing structured clip diagnostic");
            check(messages.back()["context"]["asset"] == Json(id), "Diagnostic lost clip AssetId");
            invalid.tick();
            check(invalid.engine.services().diagnostics().size() == messages.size(),
                  "Invalid clip retried/spammed every tick");
        }
        const auto broken = root / "broken";
        std::filesystem::create_directories(broken);
        std::ofstream(broken / "forge.assets.json") << "invalid";
        {
            EngineContext optional(WorldRole::Runtime, false,
                                   {audio_module({broken, AudioOutput::Offline, false})});
            check(!optional.services().available(Capability::Audio),
                  "Failed optional backend published Audio capability");
            check(!optional.services().diagnostics().empty(), "Optional failure disappeared");
        }
        reject([&] {
            EngineContext required(WorldRole::Runtime, false,
                                   {audio_module({broken, AudioOutput::Offline, true})});
        });
        wav(root / "Assets/stereo.wav", 2, 24000);
        auto stereo_record = AssetCatalog::register_audio_clip(root, "Assets/stereo.wav");
        {
            Fixture stereo(root);
            stereo.load(source(stereo_record.id));
            stereo.simulation.audio_paused(false);
            std::vector<float> pcm(4096);
            stereo.audio->read_offline(pcm);
            double energy = 0;
            for (std::size_t i = 0; i < pcm.size(); i += 2) {
                energy += pcm[i] * pcm[i];
                check(std::abs(pcm[i] + pcm[i + 1]) < 1e-5f, "Stereo channels collapsed");
            }
            check(energy > 1, "24 kHz stereo WAV did not decode/resample");
        }
        auto doc = source(record.id);
        {
            Fixture uninterrupted(root), paused(root);
            uninterrupted.load(doc);
            paused.load(doc);
            uninterrupted.simulation.audio_paused(false);
            paused.simulation.audio_paused(false);
            std::vector<float> a(2048), b(2048), silence(2048);
            uninterrupted.audio->read_offline(a);
            paused.audio->read_offline(b);
            paused.simulation.audio_paused(true);
            for (int i = 0; i < 8; i++) {
                paused.audio->read_offline(silence);
                for (float x : silence)
                    check(x == 0, "Paused PCM not silent");
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            paused.tick();
            paused.simulation.audio_paused(false);
            uninterrupted.audio->read_offline(a);
            paused.audio->read_offline(b);
            for (std::size_t i = 0; i < a.size(); i++)
                check(std::abs(a[i] - b[i]) < 1e-6f, "Pause advanced or reset playback cursor");
            Fixture faster(root);
            auto fast = doc;
            fast["entities"][0]["components"]["forge.audio_source"]["pitch"] = 2;
            faster.load(fast);
            faster.simulation.audio_paused(false);
            a.resize(9600);
            b.resize(9600);
            uninterrupted.audio->read_offline(a);
            faster.audio->read_offline(b);
            auto crossings = [](const auto& pcm) {
                unsigned n = 0;
                for (std::size_t i = 2; i < pcm.size(); i += 2)
                    if (pcm[i - 2] <= 0 && pcm[i] > 0)
                        ++n;
                return n;
            };
            check(crossings(b) > crossings(a) * 1.8 && crossings(b) < crossings(a) * 2.2,
                  "Pitch multiplier did not change sample frequency");
        }
        Fixture f(root);
        f.load(doc);
        check(f.audio->status()["voices"] == 1, "Source realization");
        const auto authored = f.scene.snapshot();
        check(f.energy() == 0, "Initial paused output audible");
        f.simulation.audio_paused(false);
        check(f.energy() > 1e-5, "Autoplay silent");
        f.simulation.audio_paused(true);
        check(f.energy() == 0, "Pause failed");
        const auto before_tick = f.audio->status()["tick"];
        f.tick();
        check(f.audio->status()["tick"] == before_tick.get<unsigned>() + 1, "Step not one tick");
        check(f.energy() == 0, "Step produced sound");
        f.audio->stop_source(f.ref());
        f.tick();
        f.audio->play(f.ref());
        f.tick();
        check(f.energy() == 0, "Play while paused audible");
        f.simulation.audio_paused(false);
        check(f.energy() > 1e-5, "Resume did not play pending intent");
        check(f.scene.snapshot() == authored, "Playback mutated authored scene");
        auto c = f.scene.entity("speaker").get<AudioSource>();
        c.gain = 0;
        f.scene.entity("speaker").set(c);
        f.tick();
        f.energy();
        check(f.energy() < 1e-9, "Gain zero failed");
        c.gain = .5;
        c.pitch = 2;
        f.scene.entity("speaker").set(c);
        f.tick();
        check(f.energy() > 1e-6, "Pitch/gain update silent");
        f.audio->stop_source(f.ref());
        f.tick();
        f.energy();
        check(f.energy() == 0, "Stop failed");
        f.audio->play(f.ref());
        f.tick();
        check(f.energy(96000) > 1e-6, "Loop does not continue past clip end");
        c.loop = false;
        f.scene.entity("speaker").set(c);
        f.tick();
        f.audio->play(f.ref());
        f.tick();
        f.energy(96000);
        check(f.energy() == 0, "Nonloop clip never ended");
        auto second = authoring_command(f.scene, "entity.duplicate", {{"entity", "speaker"}})
                          .at("selected")
                          .get<std::string>();
        f.tick();
        check(f.audio->status()["voices"] == 2 && f.audio->status()["clips"] == 1,
              "Duplicate source did not share clip");
        f.scene.entity(second).remove<AudioSource>();
        f.tick();
        check(f.audio->status()["voices"] == 1, "Removal leaked voice");
        const auto old = f.ref();
        authoring_command(f.scene, "entity.delete", {{"entity", "speaker"}});
        f.tick();
        check(f.audio->status()["voices"] == 0, "Delete leaked voice");
        reject([&] { f.audio->play(old); });
        f.load(doc);
        check(f.audio->status()["voices"] == 1, "Scene replacement failed");
        // Spatial source/listener use the regular transform graph, including scaled parents.
        auto listener = authoring_command(f.scene, "entity.create", {{"name", "Listener"}})
                            .at("selected")
                            .get<std::string>();
        authoring_command(f.scene, "component.add",
                          {{"entity", listener}, {"component", "forge.audio_listener"}});
        c = f.scene.entity("speaker").get<AudioSource>();
        c.spatialized = true;
        f.scene.entity("speaker").set(c);
        f.tick();
        f.audio->play(f.ref());
        f.tick();
        check(f.energy() > 1e-5, "Spatial source at listener silent");
        auto l2 = authoring_command(f.scene, "entity.duplicate", {{"entity", listener}})
                      .at("selected")
                      .get<std::string>();
        f.tick();
        f.energy();
        check(f.energy() < 1e-9, "Multiple listeners selected nondeterministically");
        f.scene.entity(l2).remove<AudioListener>();
        f.tick();
        check(f.energy() > 1e-5, "Listener recovery failed");
        auto parent = authoring_command(f.scene, "entity.create", {{"name", "Parent"}})
                          .at("selected")
                          .get<std::string>();
        f.scene.entity(parent).set<LocalTranslation>({10, 0, 0});
        f.scene.reparent_entity("speaker", parent, ReparentMode::KeepLocal);
        f.tick();
        f.energy();
        const auto distant = f.energy();
        check(distant < .001, "Parent source attenuation missing");
        f.scene.entity(listener).set<LocalTranslation>({10, 0, 0});
        f.tick();
        f.energy();
        check(f.energy() > distant * 2, "Moving listener not synchronized");
        f.scene.entity(parent).set<LocalScale>({3, 2, 4});
        f.tick();
        const auto scaled = f.energy();
        check(scaled > distant * 2, "Scale changed audio range");
        f.scene.entity("speaker").set<SpatialBinding>({SpatialMode::World, {}});
        f.tick();
        f.energy();
        check(f.energy() < scaled * .5, "World binding ignored");
        f.scene.entity("speaker").set<SpatialBinding>(
            {SpatialMode::Explicit,
             f.engine.world().reference(f.scene.entity(parent).id()).value()});
        f.tick();
        f.energy();
        check(f.energy() > distant * 2, "Explicit spatial parent ignored");
        // Missing/wrong source types fail this voice honestly and leave the runtime usable.
        c.clip.id = AssetId::generate();
        f.scene.entity("speaker").set(c);
        f.tick();
        check(f.audio->status()["failed_sources"] == 1, "Missing clip not diagnosed");
        c.clip.id = record.id;
        c.pitch = 100;
        f.scene.entity("speaker").set(c);
        f.tick();
        check(f.audio->status()["voices"] == 0, "Invalid native configuration accepted");
        c.pitch = 1;
        f.scene.entity("speaker").set(c);
        f.tick();
        check(f.audio->status()["voices"] == 1, "Corrected source not recovered");
        // Scene/property API bool and reference validation plus undo boundaries.
        Fixture author(root);
        author.load(doc);
        auto saved = author.scene.snapshot();
        authoring_command(author.scene, "property.set",
                          {{"entity", "speaker"},
                           {"component", "forge.audio_source"},
                           {"field", "loop"},
                           {"value", false}});
        check(!author.scene.entity("speaker").get<AudioSource>().loop, "Boolean property failed");
        author.scene.undo();
        check(author.scene.snapshot() == saved, "Audio undo failed");
        reject([&] {
            authoring_command(author.scene, "property.set",
                              {{"entity", "speaker"},
                               {"component", "forge.audio_source"},
                               {"field", "clip"},
                               {"value", "Assets/moved.wav"}});
        });
        check(author.scene.snapshot() == saved, "Invalid reference mutated scene");
        PrefabLibrary library(root);
        auto prefab = library.create(author.scene, create_prefab_source(author.scene, "speaker"),
                                     "speaker.prefab.json");
        auto a = instantiate_prefab(author.scene, prefab),
             b = instantiate_prefab(author.scene, prefab);
        check(!author.scene.entity(a).owns<AudioSource>(), "Inherited source made owned");
        authoring_command(
            author.scene, "property.set",
            {{"entity", a}, {"component", "forge.audio_source"}, {"field", "gain"}, {"value", 1}});
        auto expected = library.source(prefab), candidate = expected;
        candidate["members"][0]["components"]["forge.audio_source"]["gain"] = .25;
        library.publish(author.scene, expected, candidate);
        check(author.scene.entity(a).get<AudioSource>().gain == 1 &&
                  author.scene.entity(b).get<AudioSource>().gain == .25f,
              "Equal override/publish failed");
        authoring_command(author.scene, "component.revert",
                          {{"entity", a}, {"component", "forge.audio_source"}});
        check(author.scene.entity(a).get<AudioSource>().gain == .25f, "Revert failed");
        authoring_command(author.scene, "property.set",
                          {{"entity", b},
                           {"component", "forge.audio_source"},
                           {"field", "loop"},
                           {"value", false}});
        authoring_command(author.scene, "property.set",
                          {{"entity", b},
                           {"component", "forge.audio_source"},
                           {"field", "clip"},
                           {"value", bad_record.id}});
        check(!author.scene.entity(b).get<AudioSource>().loop &&
                  author.scene.entity(b).get<AudioSource>().clip.id == bad_record.id,
              "Prefab bool/reference overrides failed");
        authoring_command(author.scene, "property.revert",
                          {{"entity", b}, {"component", "forge.audio_source"}, {"field", "clip"}});
        check(author.scene.entity(b).get<AudioSource>().clip.id == record.id &&
                  !author.scene.entity(b).get<AudioSource>().loop,
              "Property Revert lost other intent");
        auto roundtrip = author.scene.snapshot();
        Fixture reopened(root);
        reopened.load(roundtrip);
        check(reopened.scene.snapshot() == roundtrip, "Prefab audio roundtrip changed");
        for (int i = 0; i < 4; i++)
            author.scene.undo();
        check(author.scene.entity(a).get<AudioSource>().gain == 1, "Revert undo failed");
        const auto wrong_ref = author.ref();
        std::thread wrong([&] { reject([&] { author.audio->play(wrong_ref); }); });
        wrong.join();
        for (int i = 0; i < 8; i++) {
            Fixture temporary(root);
            temporary.load(doc);
            temporary.simulation.audio_paused(false);
            check(temporary.energy() > 1e-5, "Repeated create failed");
            temporary.audio->shutdown();
            reject([&] { temporary.audio->play(old); });
        }
        for (int i = 0; i < 5; i++) {
            EngineContext null_device(WorldRole::Runtime, false,
                                      {audio_module({root, AudioOutput::NullDeviceTest, true})});
            Scene scene(null_device.world());
            scene.restore_snapshot(doc);
            auto audio = std::static_pointer_cast<AudioRuntime>(null_device.services().audio());
            audio->synchronize(0);
            audio->paused(false);
            for (int attempt = 0; attempt < 20 && audio->status().at("mixed_frames") == 0;
                 ++attempt)
                std::this_thread::sleep_for(std::chrono::milliseconds(5));
            check(audio->status().at("mixed_frames").get<unsigned>() > 0,
                  "Null device never executed callback");
        }
        std::filesystem::remove_all(root);
        std::cout << "Audio core/assets/pause/spatial/prefab/teardown tests passed (offline, no "
                     "hardware claim)\n";
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
