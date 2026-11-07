#pragma once
#include <algorithm>
#include <cmath>
#include <forge/application_state_components.hpp>
#include <forge/application_states.hpp>
#include <forge/assets.hpp>
#include <forge/audio.hpp>
#include <forge/authoring.hpp>
#include <forge/prefab_authoring.hpp>
#include <forge/runtime.hpp>
#include <forge/runtime_world.hpp>
#include <fstream>
#include <iostream>
#include <thread>
using namespace forge;
inline void check(bool b, const char* text) {
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
inline void wav(const std::filesystem::path& path, unsigned channels = 1, unsigned rate = 48000,
                bool silent_intro = false) {
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
            u(std::uint16_t(std::int16_t((silent_intro && i < rate / 4 ? 0
                                          : channel                    ? -1
                                                                       : 1) *
                                         3000 * std::sin(i * 2 * 3.141592653589793 * 440 / rate))),
              2);
}
inline Json source(AssetId clip) {
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
    EngineContext engine;
    Scene scene;
    RuntimeSimulation simulation;
    std::shared_ptr<AudioRuntime> audio;
    explicit Fixture(const std::filesystem::path& root, float volume = 1, bool physics = false,
                     AudioRouting routing = {})
        : engine(
              WorldRole::Runtime, false,
              [&] {
                  std::vector<EngineModule> modules{audio_module(
                      {root, AudioOutput::Offline, true, volume, {1, 1, 1}, std::move(routing)})};
                  if (physics)
                      modules.push_back(physics_module());
                  return modules;
              }()),
          scene(engine.world()), simulation(engine.world(), scene),
          audio(std::static_pointer_cast<AudioRuntime>(engine.services().audio())) {}
    void load(const Json& doc) {
        scene.restore_snapshot(doc);
        simulation.reset_presentation();
        simulation.sync_audio();
        (void)audio->wait_for_sources(std::chrono::seconds(5));
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
    void tick() {
        simulation.tick(1.f / 60);
        if (audio->status().at("pending_sources") != 0)
            (void)audio->wait_for_sources(std::chrono::seconds(5));
    }
};
