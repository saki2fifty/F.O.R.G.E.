#include "fbx_binary_fixture.hpp"
#include "fbx_fixture.hpp"
#include "fbx_native.hpp"
#include "gltf_model_cook.hpp"
#include <algorithm>
#include <cmath>
#include <forge/transform.hpp>
#include <iostream>
using namespace forge;
using namespace forge::asset_detail;
namespace {
void check(bool ok, const char* why) {
    if (!ok)
        throw std::runtime_error(why);
}
template <class F> void rejects(F&& fn, const char* why) {
    try {
        fn();
    } catch (const std::exception&) {
        return;
    }
    throw std::runtime_error(why);
}
} // namespace
#include "fbx_native_edge_tests.hpp"
int main() {
    try {
        check_fbx_edges();
        for (bool reversed : {false, true}) {
            for (double unit : {100., 1.}) {
                const auto source = test::fbx_triangle(reversed, unit);
                const auto bytes = std::as_bytes(std::span(source));
                const auto info = inspect_fbx(bytes);
                check(!info.at("animation").get<bool>() && info.at("images").empty(),
                      "Static FBX inspection returned incorrect dependencies");
                auto prepared = convert_fbx_interchange(bytes, {}, nlohmann::json::object());
                check(prepared.evidence.at("/meshes/0").exporter_key == "fbx:mesh:1001",
                      "FBX mesh lost its original source UID");
                std::size_t node = 0;
                for (const auto& [address, evidence] : prepared.evidence)
                    if (evidence.exporter_key == "fbx:node:1002")
                        node = std::stoul(address.substr(7));
                const auto& t = prepared.generated.document.at("nodes").at(node).at("translation");
                check(std::abs(t[0].get<double>() - 2 * unit / 100) < 1e-9 &&
                          std::abs(t[1].get<double>() - 3 * unit / 100) < 1e-9 &&
                          std::abs(t[2].get<double>() - 4 * unit / 100) < 1e-9,
                      "FBX local transforms were not converted to metres");
                NativeGltfDocument native(std::move(prepared.generated));
                const auto primitive = native.primitive(0, 0);
                check(std::abs(primitive.maximum[0] - unit / 100) < 1e-6 &&
                          std::abs(primitive.maximum[1] - unit / 100) < 1e-6,
                      "FBX geometry was not converted to metres");
                const auto& uv = primitive.attributes.at("TEXCOORD_0").values;
                check(uv == std::vector<float>{0, 1, 1, 1, 0, 0},
                      "FBX texture coordinates were not converted to image orientation");
                auto files = cook_static_gltf_bundle(native);
                const auto bundle = validate_model_bundle(files);
                check(bundle.members.size() == 3 && bundle.hierarchy.at("nodes").size() == 2,
                      "FBX interchange did not reuse the canonical model cook");
            }
        }
        for (const auto version : {7400u, 7500u})
            for (const bool reversed : {false, true}) {
                const auto source = test::fbx_binary_triangle(version, reversed);
                auto prepared = convert_fbx_interchange(std::as_bytes(std::span(source)), {},
                                                        nlohmann::json::object());
                check(prepared.evidence.at("/meshes/0").exporter_key == "fbx:mesh:1001",
                      "Binary FBX lost source UID identity");
                NativeGltfDocument native(std::move(prepared.generated));
                const auto primitive = native.primitive(0, 0);
                check(primitive.vertex_count == 3 &&
                          primitive.maximum == std::array<float, 3>{1, 1, 0},
                      "Binary FBX geometry differs from the original triangle");
                check(!cook_static_gltf_bundle(native).empty(), "Binary FBX canonical cook failed");
                const auto truncated = source.substr(0, source.size() / 2);
                rejects([&] { (void)inspect_fbx(std::as_bytes(std::span(truncated))); },
                        "Truncated binary FBX was accepted");
            }
        const auto broken = std::string("invalid FBX");
        for (bool skinned : {false, true}) {
            const auto source = test::fbx_animated_triangle(skinned);
            const auto bytes = std::as_bytes(std::span(source));
            check(inspect_fbx(bytes).at("animation"), "FBX clip was not discovered");
            auto prepared = convert_fbx_interchange(bytes, {}, nlohmann::json::object());
            check(prepared.evidence.at("/animations/0").exporter_key == "fbx:clip:2000" &&
                      prepared.evidence.at("/rig/skeleton").exporter_key == "fbx:combined-rig",
                  "FBX animation source identity is missing");
            NativeGltfDocument native(std::move(prepared.generated));
            const auto animation = native.animation(0);
            check(std::abs(animation.duration - 1) < 1e-6 && !animation.tracks.empty(),
                  "FBX clip lost its duration or tracks");
            bool moved = false;
            for (const auto& track : animation.tracks)
                if (track.path == NativeAnimationPath::Translation && track.values->size() >= 6)
                    moved |= std::abs(track.values->front() - 2) < 1e-6 &&
                             std::abs(track.values->at(track.values->size() - 2) - 5) < 1e-6;
            check(moved, "FBX baked movement did not reach the expected endpoint");
            auto geometry = cook_gltf_geometry_bundle(native);
            check(validate_model_bundle(geometry, ModelValidation::GeometryStage)
                      .hierarchy.at("animation_pending"),
                  "FBX animated geometry was treated as a complete family");
            if (skinned) {
                const auto p = native.primitive(0, 0);
                check(p.integer_attributes.at("JOINTS_0").values ==
                          std::vector<std::uint32_t>(12, 0),
                      "FBX joint indices were not preserved");
                const auto binding = native.inverse_bind_matrices(0);
                check(binding.size() == 1 && binding[0][12] == 2 && binding[0][13] == 3 &&
                          binding[0][14] == 4,
                      "FBX inverse bind differs from the original mesh-to-bone transform");
            }
        }
        rejects([&] { (void)inspect_fbx(std::as_bytes(std::span(broken))); },
                "Malformed FBX was accepted");
        std::stop_source cancel;
        cancel.request_stop();
        const auto source = test::fbx_triangle();
        rejects([&] { (void)inspect_fbx(std::as_bytes(std::span(source)), cancel.get_token()); },
                "Cancelled FBX parse was accepted");
        std::cout << "Original FBX source UID/order, metre transforms/geometry, UVs, canonical "
                     "cook, malformed/cancelled input passed\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
