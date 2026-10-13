#include "audio_resource.hpp"
#include "audio_source_access.hpp"
#include <chrono>
#include <condition_variable>
#include <forge/audio.hpp>
#include <forge/game_session.hpp>
#include <forge/runtime.hpp>
#include <iostream>
#include <thread>
using namespace forge;
namespace {
void require(bool value, const char* message) {
    if (!value)
        throw std::runtime_error(message);
}
Json document(AssetId clip, unsigned count = 2) {
    auto rows = Json::array();
    for (unsigned i = 0; i < count; ++i)
        rows.push_back({{"id", "speaker" + std::to_string(i)},
                        {"name", "Speaker"},
                        {"components",
                         {{"forge.audio_source",
                           {{"clip", clip},
                            {"play_on_start", true},
                            {"loop", true},
                            {"gain", 1},
                            {"pitch", 1},
                            {"spatialized", false},
                            {"minimum_distance", 1},
                            {"maximum_distance", 100}}}}}});
    return {{"version", 1}, {"entities", rows}};
}
struct Running {
    EngineContext engine;
    Scene scene;
    RuntimeSimulation simulation;
    std::shared_ptr<AudioRuntime> audio;
    explicit Running(const std::filesystem::path& root)
        : engine(WorldRole::Runtime, false, {audio_module({root, AudioOutput::Offline, true})}),
          scene(engine.world()), simulation(engine.world(), scene),
          audio(std::static_pointer_cast<AudioRuntime>(engine.services().audio())) {}
    EntityRef ref(unsigned i = 0) const { return scene.reference("speaker" + std::to_string(i)); }
    double energy() {
        std::array<float, 8192> samples{};
        audio->read_offline(samples);
        double result = 0;
        for (auto sample : samples)
            result += double(sample) * sample;
        return result;
    }
};
void controlled_pool(const std::filesystem::path& root, const AssetRecord& record) {
    ResourcePool<AudioClipAsset> pool{{1, 2, 2, 1024 * 1024}};
    std::mutex mutex;
    std::condition_variable_any changed;
    bool entered = false, release = false;
    const auto owner = std::this_thread::get_id();
    std::thread::id worker;
    auto blocked = pool.request(AssetRef<AudioClipAsset>{record.id}, audio_clip_revision(record), 1,
                                [&](std::stop_token stop) {
                                    std::unique_lock lock(mutex);
                                    worker = std::this_thread::get_id();
                                    entered = true;
                                    changed.notify_all();
                                    changed.wait(lock, stop, [&] { return release; });
                                    throw std::runtime_error("Controlled cancellation");
                                    return ResourceCandidate<AudioClipAsset>{};
                                });
    {
        std::unique_lock lock(mutex);
        require(changed.wait_for(lock, std::chrono::seconds(5), [&] { return entered; }),
                "Controlled loader did not start");
        require(worker != owner, "Audio preparation ran on owner thread");
    }
    auto shared = request_audio_clip(pool, root, record);
    require(shared.inspect().identity == blocked.inspect().identity &&
                pool.statistics().pending == 1 && pool.pump() == 0,
            "Selected audio request did not coalesce without owner waiting");
    pool.unload(AssetRef<AudioClipAsset>{record.id});
    auto replacement = request_audio_clip(pool, root, record);
    require(replacement.inspect().identity != blocked.inspect().identity,
            "Cancelled generation was reused");
    {
        std::lock_guard lock(mutex);
        release = true;
    }
    changed.notify_all();
    require(pool.wait(replacement, std::chrono::seconds(5)) &&
                blocked.inspect().state != ResourceState::Ready,
            "Late cancelled completion replaced the current audio selection");
    auto lease = pool.acquire(replacement);
    require(lease && lease->frames > 0, "Selected audio lease has no admitted PCM");
    pool.unload(AssetRef<AudioClipAsset>{record.id});
    pool.collect();
    require(lease->frames > 0 && pool.statistics().memory.cpu_asset > 0,
            "Retirement freed PCM still pinned by a consumer");
    lease = {};
    pool.collect();
    require(pool.statistics().memory.total() == 0, "Released PCM was not collected");
    ResourcePool<AudioClipAsset> bounded{{1, 1, 1, 1}};
    auto oversized = request_audio_clip(bounded, root, record);
    require(!bounded.wait(oversized, std::chrono::seconds(5)) &&
                oversized.inspect().state == ResourceState::Failed &&
                bounded.statistics().memory.total() == 0,
            "Audio candidate exceeded the pool memory bound");
}
void runtime_controls(const std::filesystem::path& root, AssetId clip, AssetId second) {
    Running r(root);
    r.scene.restore_snapshot(document(clip));
    const auto saved = r.scene.snapshot();
    r.audio->stop_source(r.ref());
    r.audio->pause_source(r.ref(1));
    const auto begin = std::chrono::steady_clock::now();
    r.simulation.tick(1.f / 60);
    const auto micros = std::chrono::duration_cast<std::chrono::microseconds>(
                            std::chrono::steady_clock::now() - begin)
                            .count();
    const auto pending = r.audio->status();
    require(pending.at("tick") == 1 && pending.at("pending_sources") == 2 &&
                pending.at("pending_clips") == 1 && pending.at("voices") == 0 &&
                pending.at("failed_sources") == 0 &&
                AudioSourceAccess::status(*r.audio, r.ref()).at("pending") == true,
            "First use did not yield a completed tick with a shared pending load");
    r.audio->paused(false);
    require(r.energy() == 0, "Pending source produced audio before admission");
    require(r.audio->wait_for_sources(std::chrono::seconds(5)), "Runtime load failed");
    require(r.audio->status().at("voices") == 2 && r.audio->status().at("clips") == 1 &&
                r.audio->status().at("playing_sources") == 0 && r.energy() == 0 &&
                AudioSourceAccess::status(*r.audio, r.ref(1)).at("source_paused") == true,
            "Pending Stop/Pause intent was lost or autoplay leaked");
    AudioSourceAccess::control(*r.audio, r.ref(1), AudioSourceOperation::Resume);
    require(r.energy() > 0 && r.scene.snapshot() == saved && r.audio->status().at("tick") == 1,
            "Prepared Resume failed or tooling changed authored values/tick");
    auto source = r.scene.entity("speaker0").get<AudioSource>();
    source.clip.id = second;
    r.scene.entity("speaker0").set(source);
    bool rejected = false;
    try {
        (void)AudioSourceAccess::status(*r.audio, r.ref());
    } catch (const std::exception&) {
        rejected = true;
    }
    require(rejected, "Changed source exposed a voice for its previous clip");
    r.audio->play(r.ref());
    r.audio->synchronize(1);
    require(r.audio->status().at("pending_sources") == 1, "Clip switch did not become pending");
    r.scene.delete_subtree("speaker0");
    require(r.audio->wait_for_sources(std::chrono::seconds(5)) &&
                r.audio->status().at("voices") == 1,
            "Deleted pending source created a late voice");
    std::cout << "first-use owner tick us=" << micros << " (observation, no FPS claim)\n";
    Running replaced(root);
    auto doc = document(clip, 1);
    replaced.scene.restore_snapshot(doc);
    replaced.audio->stop_source(replaced.ref());
    replaced.audio->synchronize(0);
    const auto previous_id = replaced.scene.entity("speaker0").id();
    replaced.scene.delete_subtree("speaker0");
    replaced.scene.restore_snapshot(doc);
    require(replaced.scene.entity("speaker0").id() != previous_id,
            "Deleted source fixture did not recreate a native generation");
    require(replaced.audio->wait_for_sources(std::chrono::seconds(5)) &&
                replaced.audio->status().at("playing_sources") == 1,
            "Stale pending command controlled a reconstructed source");
    Running full(root);
    full.scene.restore_snapshot(document(clip, 257));
    full.audio->synchronize(0);
    require(full.audio->status().at("pending_sources") == 256 &&
                full.audio->status().at("failed_sources") == 1,
            "Pending sources bypassed the voice bound");
    require(!full.audio->wait_for_sources(std::chrono::seconds(5)),
            "Failed required source was reported ready");
    Running paused(root);
    paused.scene.restore_snapshot(document(clip, 1));
    paused.audio->synchronize(7);
    const auto pose = paused.scene.snapshot();
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (paused.audio->status().at("pending_sources") != 0) {
        paused.audio->poll_loading();
        require(std::chrono::steady_clock::now() < deadline, "Paused audio admission stalled");
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    require(paused.audio->status().at("tick") == 7 && paused.audio->status().at("paused") == true &&
                paused.audio->status().at("voices") == 1 && paused.energy() == 0 &&
                paused.scene.snapshot() == pose,
            "Between-tick loading changed time, authored state or paused output");
    // Destroy with a live request as well as with admitted callback data.
}
void session_readiness(const std::filesystem::path& root, AssetId clip) {
    GameSessionConfig config;
    config.content_root = root;
    config.audio = AudioConfig{root, AudioOutput::Offline, true};
    GameSession session(config);
    auto ticket = session.prepare(document(clip, 1));
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (!session.poll_preparation(ticket).ready) {
        require(std::chrono::steady_clock::now() < deadline, "Session audio preparation stalled");
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    session.activate(ticket, RuntimeClock::Clock::now(), false);
    const auto original = session.presentation();
    bool rejected = false;
    try {
        ticket = session.prepare(document(AssetId::generate(), 1));
        session.poll_preparation(ticket);
    } catch (const std::exception&) {
        rejected = true;
    }
    require(rejected && session.presentation() == original,
            "Failed candidate replaced the last-good session");
}
} // namespace
int main(int argc, char** argv) {
    try {
        require(argc == 3, "Need Gallery and private scratch");
        const auto root = std::filesystem::path(argv[2]) / AssetId::generate().str();
        std::filesystem::create_directories(root / "Assets");
        for (const auto* name : {"first.wav", "second.wav"})
            std::filesystem::copy_file(std::filesystem::path(argv[1]) / "Assets/FalloffTone.wav",
                                       root / "Assets" / name);
        const auto first = AssetCatalog::register_audio_clip(root, "Assets/first.wav");
        const auto second = AssetCatalog::register_audio_clip(root, "Assets/second.wav");
        controlled_pool(root, first);
        runtime_controls(root, first.id, second.id);
        session_readiness(root, first.id);
        std::filesystem::remove_all(root);
        std::cout << "Audio worker/coalescing/bounds/lifetime, pending controls, deletion and "
                     "scene readiness passed\n";
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
