#pragma once
#include "frame_renderer.hpp"
#include "sprite_draw_render_tests.hpp"
#include <algorithm>
#include <fstream>
#include <iostream>
namespace forge::test_reflection_probes {
template <class Render>
void gallery(Render&& render, FrameRenderer& frame, FrameRenderer& ordinary,
             const RenderScene& source, const std::filesystem::path& images) {
    using test_sprite_draw::check;
    check(source.reflection_probes.size() == 2 && source.meshes.size() == 4 &&
              source.reflection_probes[0].probe.texture !=
                  source.reflection_probes[1].probe.texture,
          "Local probe Gallery lost its two maps or four metallic surfaces");
    auto disabled = source;
    disabled.reflection_probes.clear();
    for (const auto& [width, height] : {std::pair{640u, 360u}, std::pair{360u, 640u}}) {
        const auto off = render(ordinary, disabled, width, height);
        const auto on = render(frame, source, width, height);
        std::size_t changed = 0;
        for (std::size_t pixel = 0; pixel < on.size(); ++pixel) {
            changed += on[pixel] != off[pixel];
            check(on[pixel][3] == off[pixel][3], "Imported probes changed Gallery alpha");
            if (off[pixel] == off.front())
                check(on[pixel] == off[pixel], "Imported probes changed clear background");
        }
        check(changed > 1000 && render(frame, source, width, height) == on,
              "Imported probe Gallery lacks a stable visible lighting signal");
        for (bool enabled : {false, true}) {
            const auto path =
                images /
                (std::string("local-reflection-gallery-") + (enabled ? "enabled-" : "disabled-") +
                 std::to_string(width) + "x" + std::to_string(height) + ".ppm");
            std::ofstream file(path, std::ios::binary);
            file << "P6\n" << width << ' ' << height << "\n255\n";
            for (const auto& pixel : enabled ? on : off)
                file.write(reinterpret_cast<const char*>(pixel.data()), 3);
            check(bool(file.flush()), "Local probe Gallery capture failed");
        }
        std::cout << "Imported probe Gallery " << width << 'x' << height << ": " << changed
                  << " changed pixels, static/alpha/background PASS\n";
    }
}
template <class Render>
void scene(Render&& render, FrameRenderer& frame, FrameRenderer& ordinary,
           const RenderScene& source, const test_sprite_draw::Pixels& reference) {
    using test_sprite_draw::check;
    RenderReflectionProbe probe;
    probe.entity = EntityId::generate();
    probe.probe.texture = source.settings.environment.texture;
    probe.probe.intensity = source.settings.environment.intensity;
    probe.probe.half_extent_x = probe.probe.half_extent_y = probe.probe.half_extent_z = 100;
    probe.probe.blend_distance = 0;
    auto local = source;
    local.reflection_probes = {probe};
    const auto unit = render(frame, local);
    unsigned maximum = 0;
    for (std::size_t pixel = 0; pixel < unit.size(); ++pixel) {
        check(unit[pixel][3] == reference[pixel][3], "Local environment replacement changed alpha");
        for (unsigned channel = 0; channel < 3; ++channel)
            maximum = std::max(maximum, unsigned(std::abs(int(unit[pixel][channel]) -
                                                          int(reference[pixel][channel]))));
    }
    check(maximum <= 1, "Matching local map changed the equivalent global specular response");
    local.reflection_probes[0].probe.intensity = 3;
    const auto bright = render(frame, local);
    std::size_t changed = 0;
    for (std::size_t pixel = 0; pixel < bright.size(); ++pixel) {
        changed += bright[pixel] != reference[pixel];
        check(bright[pixel][3] == reference[pixel][3], "Local reflections changed Game alpha");
        if (reference[pixel] == reference.front())
            check(bright[pixel] == reference[pixel], "Local reflections changed clear background");
    }
    check(changed > 100, "Actual Game has no local probe lighting signal");
    check(render(frame, local) == bright, "Static local probe Game changed between frames");
    for (unsigned zero = 0; zero < 3; ++zero) {
        auto bypass = local;
        if (zero == 0)
            bypass.reflection_probes[0].probe.enabled = false;
        if (zero == 1)
            bypass.reflection_probes[0].probe.texture = {};
        if (zero == 2)
            bypass.reflection_probes[0].probe.intensity = 0;
        check(render(frame, bypass) == reference, "Inactive local probe changed ordinary lighting");
    }
    (void)render(frame, local);
    auto failed = local;
    failed.reflection_probes[0].probe.texture = {AssetId::generate()};
    check(render(frame, failed, 320, 180, true) == bright &&
              std::any_of(frame.diagnostics().begin(), frame.diagnostics().end(),
                          [](const auto& value) {
                              return value.category == "render.reflection_probe.resource" &&
                                     value.text.find("previous probe map retained") !=
                                         std::string::npos;
                          }),
          "Failed probe map replacement lost its last complete map or actionable diagnostic");
    auto invalid = local;
    invalid.reflection_probes[0].world.m[0] = 0;
    check(render(frame, invalid, 320, 180, true) == reference,
          "Singular probe volume retained stale influence");
    auto distant = local;
    distant.reflection_probes[0].world.m[3] = 1e100;
    check(render(frame, distant, 320, 180, true) == reference &&
              std::any_of(frame.diagnostics().begin(), frame.diagnostics().end(),
                          [](const auto& value) {
                              return value.category == "render.reflection_probe.view";
                          }),
          "Unrepresentable probe center rejected a complete camera or lacked a view diagnostic");
    auto oversized = local;
    oversized.reflection_probes[0].world.m[0] = oversized.reflection_probes[0].world.m[5] =
        oversized.reflection_probes[0].world.m[10] = 1e100;
    check(render(frame, oversized, 320, 180, true) == reference,
          "Unrepresentable probe planes entered an otherwise valid view");
    auto excessive = local;
    for (unsigned index = 1; index < 9; ++index) {
        auto extra = probe;
        extra.entity = EntityId::generate();
        excessive.reflection_probes.push_back(extra);
    }
    check(render(frame, excessive, 320, 180, true) == reference &&
              std::any_of(frame.diagnostics().begin(), frame.diagnostics().end(),
                          [](const auto& value) {
                              return value.category == "render.reflection_probe.resource" &&
                                     value.severity == Severity::Warning &&
                                     value.text.find("8 active") != std::string::npos;
                          }),
          "Over-capacity probes selected a silent subset or lost global lighting");
    auto priority = local;
    auto winner = probe;
    winner.entity = EntityId::generate();
    winner.probe.priority = 10;
    winner.probe.intensity = .5;
    priority.reflection_probes.push_back(winner);
    auto expected = source;
    expected.reflection_probes = {winner};
    check(render(frame, priority) == render(ordinary, expected),
          "Probe priority did not choose the complete covering volume");
    priority.reflection_probes[1].probe.priority = 0;
    expected.reflection_probes = {priority.reflection_probes[probe.entity < winner.entity ? 0 : 1]};
    const auto tie = render(ordinary, expected);
    check(render(frame, priority) == tie,
          "Equal-priority probes did not use stable entity ordering");
    std::reverse(priority.reflection_probes.begin(), priority.reflection_probes.end());
    check(render(frame, priority) == tie, "Input ordering changed overlapping local reflections");
    auto dark = source;
    dark.settings.environment.intensity = 0;
    const auto dark_reference = render(ordinary, dark);
    dark.reflection_probes = local.reflection_probes;
    check(render(frame, dark) != dark_reference,
          "Local specular vanished with zero global environment intensity");
    for (unsigned effects = 1; effects < 4; ++effects) {
        auto combined = local;
        combined.settings.screen_space_reflections.enabled = true;
        combined.settings.ambient_occlusion.enabled = effects >= 2;
        combined.settings.temporal_anti_aliasing.enabled = effects == 3;
        combined.settings.motion_blur.enabled = effects == 3;
        combined.settings.depth_of_field.enabled = effects == 3;
        (void)render(frame, combined);
        check(!frame.pending(), "Combined local reflection frame retained unresolved resources");
    }
    check(render(frame, source) == reference, "Removing local probes changed ordinary lighting");
    std::cout << "Actual probe Game: " << changed << " changed pixels, matching-map max byte error "
              << maximum << "; zero/failure/capacity/priority/identity/combined/removal PASS\n";
}
} // namespace forge::test_reflection_probes
