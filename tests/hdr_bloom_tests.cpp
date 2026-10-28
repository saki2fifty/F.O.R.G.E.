#include "asset_reference_impact.hpp"
#include "auto_exposure_plan.hpp"
#include "hdr_bloom_plan.hpp"
#include "scene_auto_exposure_tests.hpp"
#include "scene_color_tests.hpp"
#include <forge/authoring.hpp>
#include <forge/scene_render_settings.hpp>
#include <iostream>
using namespace forge;
namespace {
void check(bool value, const char* why) {
    if (!value)
        throw std::runtime_error(why);
}
template <class F> void rejects(F fn) {
    bool rejected = false;
    try {
        fn();
    } catch (const std::exception&) {
        rejected = true;
    }
    check(rejected, "Malformed bloom input admitted");
}
void plans() {
    const auto tiny = hdr_bloom_plan(9, 7, {2, 3, 7, 3}, 16);
    check(tiny.width == 7 && tiny.height == 3 && tiny.half_width == 4 && tiny.half_height == 2 &&
              tiny.bytes == (21 + 16) * 8,
          "Odd view payload or dimensions wrong");
    const auto one = hdr_bloom_plan(1, 1, {0, 0, 1, 1}, 1, 24);
    check(one.bytes == 24, "Single pixel payload wrong");
    rejects([&] { hdr_bloom_plan(1, 1, {0, 0, 1, 1}, 1, 23); });
    for (auto area : {PixelViewport{0, 0, 0, 1}, PixelViewport{8, 0, 2, 1},
                      PixelViewport{0, 6, 1, 2}, PixelViewport{UINT32_MAX, 0, 1, 1}})
        rejects([&] { hdr_bloom_plan(9, 7, area, 16); });
    rejects([&] { hdr_bloom_plan(9, 7, {0, 0, 1, 1}, 8); });
    rejects([&] {
        hdr_bloom_plan(UINT32_MAX, UINT32_MAX, {0, 0, UINT32_MAX, UINT32_MAX}, UINT32_MAX,
                       UINT64_MAX);
    });
    rejects([&] { hdr_bloom_plan(8192, 8192, {0, 0, 8192, 8192}, 16384); });
    check(hdr_bloom_plan(3840, 2160, {0, 0, 3840, 2160}, 16384).bytes == 99532800,
          "4K payload oracle wrong");
}
void settings_and_history() {
    EngineContext engine;
    Scene scene(engine.world());
    auto extended = scene.document();
    extended["rendering"] = {{"version", 1u}, {"bloom", {{"plugin", {{"opaque", 9}}}}}};
    scene.reset(extended);
    const auto original = scene.document();
    check(scene_render_settings(original).bloom == SceneBloom{}, "Old Scene enabled bloom");
    authoring_command(
        scene, "scene.rendering.set",
        {{"bloom", {{"enabled", true}, {"threshold", 2}, {"intensity", .5}, {"radius", 3}}}});
    const auto enabled = scene.document();
    check(scene_render_settings(enabled).bloom == SceneBloom{true, 2, .5, 3},
          "Bloom settings did not persist");
    check(scene.undo() && scene.document() == original && scene.redo() &&
              scene.document() == enabled,
          "Bloom did not use atomic Scene history");
    authoring_command(scene, "scene.rendering.set", {{"bloom", {{"intensity", 1}}}});
    check(scene_render_settings(scene.document()).bloom == SceneBloom{true, 2, 1, 3} &&
              scene.document()["rendering"]["bloom"]["plugin"] ==
                  enabled["rendering"]["bloom"]["plugin"],
          "Partial bloom edit lost known or opaque settings");
    const auto good = scene.document();
    auto known = good;
    known["rendering"]["bloom"].erase("plugin");
    const std::array known_documents{AssetReferenceDocument{"Bloom.scene.json", known}};
    check(collect_asset_references(scene.schema(), known_documents).uninspected.empty(),
          "Known bloom fields remained opaque to export");
    const std::array extended_documents{AssetReferenceDocument{"Bloom.scene.json", good}};
    const auto extended_impact = collect_asset_references(scene.schema(), extended_documents);
    check(extended_impact.uninspected.size() == 1 &&
              extended_impact.uninspected[0].find("rendering.bloom.plugin") != std::string::npos,
          "Unknown bloom extensions were silently marked inspected");
    for (const Json& bad :
         {Json(1), Json(nullptr), Json{{"enabled", 1}}, Json{{"threshold", -1}},
          Json{{"threshold", 65505}}, Json{{"threshold", "one"}}, Json{{"intensity", 4.00001}},
          Json{{"intensity", -.01}}, Json{{"radius", .49999}}, Json{{"radius", 16.001}}}) {
        rejects([&] { authoring_command(scene, "scene.rendering.set", {{"bloom", bad}}); });
        check(scene.document() == good, "Rejected bloom edit mutated Scene");
    }
    for (float bad :
         {std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::infinity(), -1.f}) {
        rejects([&] { validate_scene_bloom({true, bad, 1, 1}); });
        rejects([&] { validate_scene_bloom({true, 1, bad, 1}); });
        rejects([&] { validate_scene_bloom({true, 1, 1, bad}); });
    }
    validate_scene_bloom({true, 65504, 4, 16});
    validate_scene_bloom({false, 0, 0, .5});
    Scene reopened(engine.world());
    reopened.reset(scene.document());
    check(reopened.document() == good && scene_render_settings(reopened.document()).bloom.enabled,
          "Reopened Scene lost bloom intent");
}
void exposure_plans() {
    const auto single = auto_exposure_plan(1, 1, 16384);
    check(single.sample_width == 1 && single.sample_height == 1 && single.groups_x == 1 &&
              single.groups_y == 1,
          "Single-pixel metering plan is empty");
    const auto odd = auto_exposure_plan(17, 9, 16384);
    check(odd.sample_width == 17 && odd.sample_height == 9 && odd.groups_x == 2 &&
              odd.groups_y == 2,
          "Partial exposure tiles were truncated");
    const auto large = auto_exposure_plan(3840, 2160, 16384);
    check(large.sample_width == 256 && large.sample_height == 256 && large.groups_x == 16 &&
              large.groups_y == 32 && auto_exposure_payload_bytes == 262224,
          "Metering work or exact buffer payload escaped the fixed profile");
    const auto row = auto_exposure_plan(16384, 1, 16384);
    check(row.sample_width == 256 && row.sample_height == 1 && row.groups_y == 1,
          "Thin HDR sources changed metering bounds");
    rejects([&] { auto_exposure_plan(0, 1, 16384); });
    rejects([&] { auto_exposure_plan(1, 0, 16384); });
    rejects([&] { auto_exposure_plan(16385, 1, 16384); });
    rejects([&] { auto_exposure_plan(1, 1, 0); });
    rejects([&] { auto_exposure_plan(UINT32_MAX, 1, UINT32_MAX); });
    check(auto_exposure_timing({}, 4).reset && auto_exposure_timing({}, 4).delta == 0 &&
              !auto_exposure_timing(3, 3).reset && auto_exposure_timing(3, 3).delta == 0 &&
              auto_exposure_timing(3, 3.125).delta == .125f &&
              auto_exposure_timing(3, 400).delta == .25f && auto_exposure_timing(400, 3).reset,
          "Exposure timing sampled simulation, advanced equal timestamps or missed a rewind");
    rejects([&] { auto_exposure_timing({}, -1); });
    rejects([&] { auto_exposure_timing({}, std::numeric_limits<double>::infinity()); });
    rejects([&] { auto_exposure_timing(std::numeric_limits<double>::quiet_NaN(), 1); });
}
} // namespace
int main() {
    try {
        plans();
        settings_and_history();
        test_scene_color_settings();
        test_scene_auto_exposure_settings();
        exposure_plans();
        std::cout << "HDR bloom admission and Scene ownership PASS\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
