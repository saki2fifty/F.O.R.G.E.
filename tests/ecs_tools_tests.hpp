#pragma once
#include <forge/audio_components.hpp>
#include <forge/ecs_tools.hpp>
#include <forge/scene.hpp>
#include <stdexcept>
struct EcsToolsProbeValue {
    float value;
};
inline void test_ecs_tools() {
    using namespace forge;
    auto check = [](bool ok, const char* why) {
        if (!ok)
            throw std::runtime_error(why);
    };
    EngineContext engine;
    Scene scene(engine.world());
    scene.reset(
        {{"version", 1},
         {"entities",
          Json::array({{{"id", "sound"}, {"name", "Sound"}, {"components", Json::object()}}})}});
    auto& world = scene.world();
    world.component<EcsToolsProbeValue>("EcsToolsProbeValue").member<float>("value");
    auto value = world.entity("Probe").set<EcsToolsProbeValue>({3});
    EcsTools tools(engine.world());
    check(tools.query("EcsToolsProbeValue").is_object(), "Native query JSON missing");
    check(tools.entity(value.id()).is_object(), "Native entity JSON missing");
    check(tools.statistics().at("memory_bytes").get<double>() > 0,
          "Flecs memory statistics absent");
    const auto before = ecs_get_world_info(world)->frame_count_total;
    auto sound = scene.entity("sound");
    sound.set<AudioSource>(AudioSource{.gain = 2});
    tools.sample(.5f);
    tools.sample(.5f);
    auto alerts = tools.alerts();
    check(!alerts.empty() && alerts[0].at("severity") == "Warning", "Native gain warning absent");
    sound.set<AudioSource>(AudioSource{.gain = 1});
    tools.sample(.5f);
    tools.sample(.5f);
    check(tools.alerts().empty(), "Resolved native alert remains active");
    check(ecs_get_world_info(world)->frame_count_total == before,
          "Inspection advanced authoring gameplay");
    check(Json::parse(tools.export_world()).is_object(), "Native world JSON missing");
    const auto gain_member = world.component<AudioSource>().lookup("gain");
    const auto gauge = tools.create_metric(gain_member, "gauge", true);
    const auto integrated = tools.create_metric(gain_member, "increment", true);
    const auto counter = tools.create_metric(gain_member, "counter", true);
    const auto count = tools.create_metric(world.id<AudioSource>(), "count", false);
    tools.sample(.5f);
    tools.sample(.5f);
    auto metrics = tools.metrics();
    auto found = [&](ecs_entity_t definition, double expected) {
        for (const auto& metric : metrics)
            if (metric.at("metric") == definition && metric.at("value") == expected)
                return true;
        return false;
    };
    check(found(gauge, 1) && found(counter, 1) && found(integrated, 1) && found(count, 1),
          "Native metric values/count/integration disagree with source");
    tools.remove_metric(integrated);
    check(!world.is_alive(integrated), "Removed metric survived");
    bool incompatible = false;
    try {
        tools.create_metric(gain_member, "count", true);
    } catch (const std::runtime_error&) {
        incompatible = true;
    }
    check(incompatible, "Incompatible metric source/kind accepted");
    for (int i = 0; i < 60; ++i)
        tools.sample(1.f);
    const auto* history = ecs_get_pair(world, EcsWorld, EcsWorldStats, EcsPeriod1m);
    check(history && history->stats &&
              history->stats->entities.count.gauge.avg[history->stats->t] > 0,
          "Native historical Stats used by REST were not sampled");
    {
        EngineContext other;
        for (int i = 0; i < 128; ++i)
            other.world().world().entity();
        EcsTools second_tools(other.world());
        second_tools.sample(.5f);
        check(second_tools.statistics().at("entities").get<double>() > 128,
              "Multi-world late diagnostic activation failed");
    }
    const auto stale = value.id();
    value.destruct();
    bool rejected = false;
    try {
        tools.entity(stale);
    } catch (const std::runtime_error&) {
        rejected = true;
    }
    check(rejected, "Stale entity generation accepted");
}
