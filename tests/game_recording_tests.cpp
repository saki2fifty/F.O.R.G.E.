#include "game_recording.hpp"
#include "game_recording_protocol.hpp"
#include <array>
#include <cmath>
#include <forge/assets.hpp>
#include <forge/game_session.hpp>
#include <fstream>
#include <iostream>
#include <limits>
#include <thread>

using namespace forge;
namespace {
void check(bool value, const char* reason) {
    if (!value)
        throw std::runtime_error(reason);
}
template <class F> void rejects(F action) {
    try {
        action();
    } catch (const std::exception&) {
        return;
    }
    throw std::runtime_error("Invalid recording operation was accepted");
}
void policy() {
    for (const auto fps : {.5, 241., std::numeric_limits<double>::quiet_NaN()})
        rejects([&] { GameRecording invalid(fps, 3); });
    for (const auto count : {0u, 4097u})
        rejects([&] { GameRecording invalid(30, count); });
    rejects([&] { GameRecording excessive(1, 700); });
    GameRecording value(30000. / 1001, 3);
    RuntimeClock clock;
    clock.resume({});
    RuntimeClock::Time last{};
    unsigned ticks = 0;
    const auto mix = [](std::span<float> pcm) {
        constexpr std::array levels{-2.f, -1.f, -.5f, 0.f, .5f, 1.f, 2.f, .25f};
        for (unsigned i = 0; i < pcm.size(); ++i)
            pcm[i] = levels[i % levels.size()];
    };
    const auto advance = [&](RuntimeClock::Time now) {
        check(now > last, "Recording callback time was not monotonic");
        last = now;
        clock.advance(now, [&](float dt) {
            check(std::abs(dt - 1.f / 60) < 1e-8f, "Recording changed fixed simulation delta");
            ++ticks;
        });
    };
    rejects([&] { (void)value.read_frame({}, advance); });
    check(!value.failed(), "Admission failure made an untouched recording unrecoverable");
    const auto first = value.read_frame(mix, advance);
    const auto rate = recording_rate(Json{{"rate", 30000u}, {"scale", 1001u}});
    const auto receipt = recorded_audio_document(0, rate, first);
    check(read_recorded_audio(receipt, 0, rate).pcm == first,
          "Recording wire receipt changed signed PCM or rational interval");
    rejects([&] { (void)read_recorded_audio(receipt, 1, rate); });
    for (const auto* field : {"frame", "start_audio_frame", "audio_frames"})
        for (const Json& invalid : {Json(true), Json(-1), Json(1.5), Json("0"), Json(999999u)}) {
            auto bad = receipt;
            bad[field] = invalid;
            rejects([&] { (void)read_recorded_audio(bad, 0, rate); });
        }
    for (const auto invalid : {"AAAA", "!!!!", "Zg==", "Zh==", "AA=A"}) {
        auto bad = receipt;
        bad["data"] = invalid;
        rejects([&] { (void)read_recorded_audio(bad, 0, rate); });
    }
    for (const Json& invalid :
         {Json(true), Json(-1), Json(0u), Json(1.25), Json("30"), Json(std::uint64_t(1) << 32)}) {
        auto bad = Json{{"rate", 30000u}, {"scale", 1001u}};
        bad["rate"] = invalid;
        rejects([&] { (void)recording_rate(bad); });
    }
    constexpr std::array<std::int16_t, 8> expected{-32768, -32768, -16384, 0,
                                                   16384,  32767,  32767,  8192};
    for (unsigned i = 0; i < expected.size(); ++i) {
        const auto bits = std::to_integer<unsigned>(first[2 * i]) |
                          (std::to_integer<unsigned>(first[2 * i + 1]) << 8);
        check(bits == std::uint16_t(expected[i]), "PCM quantization or endianness differs");
    }
    check(first.size() == 1602 * 4 && value.read_frame(mix, advance).size() == 1601 * 4 &&
              value.read_frame(mix, advance).size() == 1602 * 4 && ticks == 6 &&
              clock.status().at("dropped_ticks") == 0 && value.status().at("audio_frames") == 4805,
          "Recording sample intervals drifted or skipped existing fixed ticks");
    check(value.time() == last && value.completed() == 3, "Recording completion lost its boundary");
    rejects([&] { (void)value.read_frame(mix, advance); });
    GameRecording broken(30, 1);
    rejects([&] {
        (void)broken.read_frame(
            [](std::span<float> pcm) { pcm[0] = std::numeric_limits<float>::quiet_NaN(); },
            advance);
    });
    check(broken.failed() && broken.completed() == 0,
          "Nonfinite audio did not fail the fresh recording");
    rejects([&] { (void)broken.read_frame(mix, advance); });
    GameRecording failed_tick(30, 1);
    rejects([&] {
        (void)failed_tick.read_frame(
            mix, [](RuntimeClock::Time) { throw std::runtime_error("Native tick failed"); });
    });
    check(failed_tick.failed(), "Native failure permitted recording retry against partial state");
    GameRecording reentrant(30, 1);
    const auto bytes = reentrant.read_frame(
        [&](std::span<float>) { rejects([&] { (void)reentrant.read_frame(mix, advance); }); },
        [](RuntimeClock::Time) {});
    check(bytes.size() == 6400 && !reentrant.failed(), "Reentrant read altered its outer interval");
}
void visit_receipts() {
    const auto a = AssetId::generate(), b = AssetId::generate();
    Json value{{"capture_scope", "session"},
               {"profile", "offline-session-game-v2"},
               {"audio_frames", 128u},
               {"visits", Json::array({{{"generation", 1u},
                                        {"scene", a},
                                        {"source", std::string(64, 'a')},
                                        {"audio_frame", 0u}}})}};
    validate_recording_visits(value, Json::object(), 1, a);
    const auto previous = value;
    value["visits"].push_back({{"generation", 2u},
                               {"scene", b},
                               {"source", std::string(64, 'b')},
                               {"audio_frame", 128u}});
    validate_recording_visits(value, previous, 2, b);
    rejects([&] { validate_recording_visits(value, previous, 1, a); });
    for (const auto* field : {"generation", "audio_frame", "scene", "source"}) {
        auto bad = value;
        bad["visits"][1][field] = true;
        rejects([&] { validate_recording_visits(bad, previous, 2, b); });
    }
    auto bad = value;
    bad["visits"][1]["audio_frame"] = 129u;
    rejects([&] { validate_recording_visits(bad, previous, 2, b); });
    bad = value;
    bad["visits"][0]["source"] = std::string(64, 'c');
    rejects([&] { validate_recording_visits(bad, previous, 2, b); });
    bad = value;
    bad["visits"][0]["audio_frame"] = 1u;
    rejects([&] { validate_recording_visits(bad, Json::object(), 2, b); });
    bad = value;
    bad["visits"][1]["generation"] = 1u;
    rejects([&] { validate_recording_visits(bad, previous, 2, b); });
}
void resumable_frames() {
    const auto generator = [](std::uint64_t& samples) {
        return [&samples](std::span<float> output) {
            for (auto& value : output)
                value = float(int(samples++ % 31) - 15) / 16;
        };
    };
    GameRecording reference(30, 1), pending(30, 1);
    std::uint64_t reference_samples = 0, pending_samples = 0;
    std::vector<RuntimeClock::Time> reference_times, pending_times;
    const auto expected = reference.read_frame(generator(reference_samples),
                                               [&](auto time) { reference_times.push_back(time); });
    bool ready = false;
    const auto advance = [&](RuntimeClock::Time time) {
        pending_times.push_back(time);
        if (pending_times.size() == 1)
            ready = false;
    };
    const auto poll = [&] {
        return pending.poll_frame(generator(pending_samples), advance, [&] { return ready; });
    };
    check(!poll() && !pending.failed() && pending.completed() == 0 && pending_samples == 0 &&
              pending.status().at("audio_frames") == 0,
          "Unready media consumed audio or became a failed frame");
    ready = true;
    check(!poll() && pending_samples == 256 && pending.status().at("audio_frames") == 128 &&
              pending.status().at("pending_audio_frames") == 128 && pending.completed() == 0,
          "Interrupted frame did not retain exactly its consumed quantum");
    check(!poll() && pending_samples == 256 && pending_times.size() == 1,
          "A preparation wait repeated audio or clock work");
    ready = true;
    const auto completed = poll();
    check(completed && *completed == expected && pending_times == reference_times &&
              pending_samples == reference_samples && pending.status() == reference.status(),
          "Resumed frame changed legacy PCM, advance times or completion metadata");

    GameRecording boundary(240, 1);
    bool boundary_ready = true;
    unsigned mixed = 0, advances = 0;
    const auto boundary_poll = [&] {
        return boundary.poll_frame(
            [&](std::span<float> output) { mixed += unsigned(output.size() / 2); },
            [&](auto) {
                ++advances;
                if (mixed == 200)
                    boundary_ready = false;
            },
            [&] { return boundary_ready; });
    };
    check(!boundary_poll() && mixed == 200 && boundary.completed() == 0 &&
              boundary.status().at("audio_frames") == 200 && !boundary.failed(),
          "Last-quantum Scene preparation prematurely completed its frame");
    check(!boundary_poll() && mixed == 200 && advances == 2,
          "Completed interval was remixed while its Scene was unready");
    boundary_ready = true;
    const auto boundary_result = boundary_poll();
    check(boundary_result && boundary_result->size() == 800 && mixed == 200 && advances == 2 &&
              boundary.completed() == 1,
          "Final readiness did not publish exactly the already consumed interval");

    GameRecording bounded(1, 1);
    unsigned work = 0, polls = 0;
    std::optional<std::vector<std::byte>> result;
    do {
        const auto before = work;
        result =
            bounded.poll_frame([](std::span<float>) {}, [&](auto) { ++work; }, [] { return true; });
        check(work - before <= GameRecording::poll_quantum_limit && ++polls <= 40,
              "Media polling exceeded its bounded work or failed to make progress");
    } while (!result);
    check(result->size() == 48000 * 4 && work == 375 && polls > 1 && bounded.completed() == 1 &&
              bounded.time() == RuntimeClock::Time{} + std::chrono::seconds(1),
          "Bounded polling changed the cumulative media boundary");

    GameRecording callback_guard(30, 1);
    const GameRecording::Mix silence = [](std::span<float>) {};
    const GameRecording::Advance idle = [](auto) {};
    check(!callback_guard.poll_frame(silence, idle,
                                     [&] {
                                         rejects([&] {
                                             (void)callback_guard.poll_frame(silence, idle,
                                                                             [] { return true; });
                                         });
                                         return false;
                                     }) &&
              !callback_guard.failed(),
          "Readiness reentry changed its outer pending frame");
    check(callback_guard.read_frame(silence, idle).size() == 6400,
          "Rejected reentry prevented subsequent valid completion");
    GameRecording failure(30, 1);
    rejects([&] {
        (void)failure.poll_frame(silence, idle, []() -> bool {
            throw std::runtime_error("Required candidate resource failed");
        });
    });
    check(failure.failed() && failure.completed() == 0 &&
              !failure.status().contains("pending_frame"),
          "Resource failure published a frame or remained retryable");
    rejects([&] { (void)failure.read_frame(silence, idle); });
}
void actual_game(const std::filesystem::path& gallery, const std::filesystem::path& root) {
    std::filesystem::create_directories(root / "Assets");
    std::filesystem::copy_file(gallery / "Assets/FalloffTone.wav", root / "Assets/tone.wav");
    const auto clip = AssetCatalog::register_audio_clip(root, "Assets/tone.wav");
    GameSessionConfig config;
    config.content_root = root;
    config.audio = AudioConfig{root, AudioOutput::Offline, true};
    GameSession game(std::move(config));
    const Json authored{
        {"version", 1},
        {"entities", Json::array({{{"id", "speaker"},
                                   {"name", "Speaker"},
                                   {"components",
                                    {{"forge.audio_source",
                                      {{"clip", clip.id},
                                       {"play_on_start", true},
                                       {"loop", true},
                                       {"gain", 1},
                                       {"pitch", 1},
                                       {"minimum_distance", 1},
                                       {"maximum_distance", 100},
                                       {"spatialized", false}}}}}},
                                  {{"id", "body"},
                                   {"name", "Falling cube"},
                                   {"components",
                                    {{"forge.position", {{"x", 0}, {"y", 5}, {"z", 0}}},
                                     {"forge.physics_body",
                                      {{"motion", 2},
                                       {"density", 1000},
                                       {"mass", 0},
                                       {"friction", .5},
                                       {"restitution", 0},
                                       {"gravity_factor", 1}}},
                                     {"forge.box_collider", {{"x", 1}, {"y", 1}, {"z", 1}}}}}}})}};
    const auto unchanged = authored;
    const auto ticket = game.prepare(authored);
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (!game.poll_preparation(ticket).ready) {
        check(std::chrono::steady_clock::now() < deadline, "Recording fixture did not prepare");
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    game.activate(ticket, {}, true);
    auto& world = game.active();
    unsigned native_calls = 0;
    auto native = world.scene.world()
                      .system()
                      .kind(world.scene.world().lookup("forge.runtime.Gameplay"))
                      .run([&](flecs::iter& it) {
                          check(std::abs(it.delta_time() - 1.f / 60) < 1e-8f,
                                "Native recording delta changed");
                          ++native_calls;
                      });
    native.add<FixedSimulation>();
    GameRecording recording(30000. / 1001, 3);
    std::vector<std::byte> all;
    for (unsigned frame = 0; frame < 3; ++frame) {
        auto pcm = recording.read_frame(
            [&](std::span<float> output) {
                std::static_pointer_cast<AudioRuntime>(game.active().engine.services().audio())
                    ->read_offline(output);
            },
            [&](RuntimeClock::Time now) {
                game.advance(now);
                game.control_frame();
            });
        all.insert(all.end(), pcm.begin(), pcm.end());
    }
    const auto y = world.scene.entity(world.scene.canonical_id("body")).get<LocalTranslation>().y;
    check(native_calls == 6 && y < 4.99 && authored == unchanged && all.size() == 4805 * 4,
          "Recording did not run native fixed systems/physics or changed authored data");
    double energy = 0;
    for (std::size_t at = 0; at < all.size(); at += 2) {
        const auto sample = std::int16_t(std::to_integer<unsigned>(all[at]) |
                                         (std::to_integer<unsigned>(all[at + 1]) << 8));
        energy += double(sample) * sample;
    }
    check(energy / (all.size() / 2) > 100, "Actual game audio was silent");
    check(game.status().at("clock").at("tick") == 6 &&
              game.status().at("clock").at("dropped_ticks") == 0,
          "Recording bypassed the existing clock or dropped its fixed ticks");
    std::ofstream output(root / "game.s16le", std::ios::binary);
    output.exceptions(std::ios::badbit | std::ios::failbit);
    output.write(reinterpret_cast<const char*>(all.data()), std::streamsize(all.size()));
    std::ofstream(root / "receipt.json") << Json{
        {"native_calls", native_calls},
        {"body_y", y},
        {"audio_frames", 4805},
        {"sample_rate", 48000},
        {"clock", game.status().at("clock")}}.dump(2);
    native.destruct();
    game.unload(recording.time());
}
} // namespace
int main(int argc, char** argv) {
    try {
        check(argc == 3, "Need Gallery and output paths");
        policy();
        resumable_frames();
        visit_receipts();
        const auto root = std::filesystem::absolute(argv[2]) / AssetId::generate().str();
        actual_game(argv[1], root);
        std::cout << "Recording policy, native fixed ticks, physics and actual stereo PCM PASS: "
                  << root << '\n';
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
