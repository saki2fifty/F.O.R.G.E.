#pragma once
#include "fog_volume_view.hpp"
#include <algorithm>
#include <vector>
namespace forge::test_fog_view {
inline void exercise() {
    const auto check = [](bool value, const char* text) {
        if (!value)
            throw std::runtime_error(text);
    };
    std::vector<RenderFogVolume> source;
    for (int i = 0; i < 18; ++i) {
        FogVolume value;
        value.priority = i;
        value.layers = 1;
        value.density = .1f + float(i) * .01f;
        source.push_back({EntityId::generate(), value, {}});
    }
    unsigned problems = 0;
    const auto report = [&](EntityId, std::string_view) { ++problems; };
    auto view = prepare_fog_view(source, {}, UINT32_MAX, report);
    const auto selected = select_fog_volumes(view, 1);
    check(problems == 0 && selected.count == 16 && selected.overflow == 2 &&
              view.front().entity == source.back().entity &&
              selected.values[0].rows[3][3] == source.back().volume.density &&
              selected.values[15].rows[3][3] == source[2].volume.density,
          "Fog priority or per-receiver source bound changed");
    std::reverse(source.begin(), source.end());
    const auto reordered = prepare_fog_view(source, {}, UINT32_MAX, report);
    check(reordered.size() == view.size(), "Fog declaration reorder lost sources");
    for (unsigned i = 0; i < view.size(); ++i)
        check(view[i].entity == reordered[i].entity && view[i].sample == reordered[i].sample,
              "Fog selection depends on declaration order");
    for (auto& row : source) {
        row.volume.priority = 0;
        row.volume.layers = 2;
    }
    FogVolume compatible;
    compatible.priority = -100;
    compatible.layers = 1;
    source.push_back({EntityId::generate(), compatible, {}});
    view = prepare_fog_view(source, {}, 3, report);
    check(select_fog_volumes(view, 1).count == 1 && select_fog_volumes(view, 0).count == 0,
          "Layer filtering happened after the Fog source bound");
    for (auto& row : source)
        row.volume.priority = 0;
    view = prepare_fog_view(source, {}, 3, report);
    check(std::is_sorted(view.begin(), view.end(),
                         [](const auto& a, const auto& b) { return a.entity < b.entity; }),
          "Equal-priority Fog sources lost durable identity ordering");
    source.resize(1);
    source[0].volume.layers = 6;
    view = prepare_fog_view(source, {}, 3, report);
    check(view.size() == 1 && select_fog_volumes(view, 5).count == 0,
          "Fog used a receiver layer hidden by the camera");
    check(select_fog_volumes(prepare_fog_view(source, {}, 7, report), 5).count == 1,
          "Shared receiver/camera layer was omitted");
    source[0].volume.enabled = false;
    check(prepare_fog_view(source, {}, 7, report).empty(), "Disabled Fog source was prepared");
    source[0].volume.enabled = true;
    source[0].volume.density = 0;
    check(prepare_fog_view(source, {}, 7, report).empty(), "Zero-density Fog source was prepared");
    source[0].volume.density = .2f;
    check(prepare_fog_view(source, {}, 0, report).empty(), "Zero camera layer mask prepared Fog");
    source.push_back(source[0]);
    source[1].entity = EntityId::generate();
    source[1].world.m[0] = 0;
    EntityId invalid;
    view = prepare_fog_view(source, {}, 7, [&](EntityId id, std::string_view message) {
        invalid = id;
        check(!message.empty(), "Fog issue lost its message");
        ++problems;
    });
    check(view.size() == 1 && problems == 1 && invalid == source[1].entity,
          "Invalid Fog transform concealed a valid source or lost scoped diagnostics");
    std::vector<RenderFogVolume> excessive(10001);
    bool rejected = false;
    try {
        (void)prepare_fog_view(excessive, {}, 1, report);
    } catch (const std::exception&) {
        rejected = true;
    }
    check(rejected, "Fog view exceeded the existing rendering entity profile");
}
} // namespace forge::test_fog_view
