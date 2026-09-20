#include "animation_asset.hpp"
#include "gltf_model_cook.hpp"
#include "gltf_ozz_transport.hpp"
#include "import_process.hpp"
#include "model_animation.hpp"
#include <cmath>
#include <fstream>
#include <iostream>
using namespace forge;
using namespace forge::asset_detail;
using Json = nlohmann::json;
void require(bool ok, const char* why) {
    if (!ok)
        throw std::runtime_error(why);
}
std::vector<std::byte> read(const std::filesystem::path& path) {
    std::ifstream f(path, std::ios::binary | std::ios::ate);
    require(bool(f), "Missing test artifact");
    const auto n = f.tellg();
    require(n >= 0 && n <= 16 * 1024 * 1024, "Invalid artifact length");
    std::vector<std::byte> b(static_cast<std::size_t>(n));
    f.seekg(0);
    f.read(reinterpret_cast<char*>(b.data()), static_cast<std::streamsize>(b.size()));
    require(bool(f), "Short artifact read");
    return b;
}
int main(int argc, char** argv) {
    try {
        require(argc == 3 || argc == 4, "Need mode and directory");
        const std::filesystem::path root = argv[2];
        if (std::string_view(argv[1]) == "prepare") {
            auto captured = capture_gltf_source(root, "input.gltf");
            const auto original = captured.document;
            NativeGltfDocument source(std::move(captured));
            auto prepared = prepare_gltf_ozz_transport(source, {30, false, 1.f});
            require(source.source().document == original, "Transport rewrote source");
            if (root.filename() == "matrix") {
                auto two = source.source();
                auto second = two.document["animations"][0];
                second["channels"][0]["target"]["path"] = "scale";
                two.document["animations"].push_back(second);
                const auto first = prepare_gltf_ozz_transport(NativeGltfDocument(two));
                std::reverse(two.document["animations"].begin(), two.document["animations"].end());
                two.document["animations"][0]["name"] = "Renamed clip";
                two.document["animations"][1]["name"] = "Renamed clip";
                const auto reordered = prepare_gltf_ozz_transport(NativeGltfDocument(two));
                for (unsigned i = 0; i < 2; ++i) {
                    require(first.metadata["clips"][i]["content_evidence"] ==
                                    reordered.metadata["clips"][1 - i]["content_evidence"] &&
                                first.metadata["clips"][i]["semantic_evidence"] ==
                                    reordered.metadata["clips"][1 - i]["semantic_evidence"],
                            "Clip reorder/rename changed semantic correspondence evidence");
                }
                require(first.metadata["content_evidence"] ==
                            reordered.metadata["content_evidence"],
                        "Clip order changed rig content identity evidence");
            }
            for (const auto& file : prepared.converter_inputs) {
                std::ofstream out(root / file.name, std::ios::binary);
                out.write(reinterpret_cast<const char*>(file.bytes.data()),
                          static_cast<std::streamsize>(file.bytes.size()));
                require(bool(out), "Transport write failed");
            }
            std::ofstream(root / "metadata.json") << prepared.metadata.dump();
            std::stop_source stop;
            stop.request_stop();
            bool rejected = false;
            try {
                prepare_gltf_ozz_transport(source, {}, stop.get_token());
            } catch (const std::exception&) {
                rejected = true;
            }
            require(rejected, "Cancelled transport accepted");
        } else if (std::string_view(argv[1]) == "convert") {
            require(argc == 4, "Need fixed converter executable");
            std::vector<ArtifactFile> inputs{{"source.gltf", read(root / "source.gltf")},
                                             {"config.json", read(root / "config.json")}};
            if (std::filesystem::exists(root / "animation.bin"))
                inputs.push_back({"animation.bin", read(root / "animation.bin")});
            const auto outputs = run_model_animation_process(argv[3], root, inputs);
            validate_model_animation(Json::parse(read(root / "metadata.json")), outputs);
            for (const auto& file : outputs) {
                std::ofstream out(root / file.name, std::ios::binary);
                out.write(reinterpret_cast<const char*>(file.bytes.data()),
                          static_cast<std::streamsize>(file.bytes.size()));
                require(bool(out), "Cannot write test converter output");
            }
            auto reject = [&](const std::vector<ArtifactFile>& bad, std::stop_token stop = {}) {
                bool rejected = false;
                try {
                    (void)run_model_animation_process(argv[3], root, bad, stop);
                } catch (const std::exception&) {
                    rejected = true;
                }
                require(rejected, "Invalid converter process input accepted");
            };
            auto bad = inputs;
            bad[0].name = "../source.gltf";
            reject(bad);
            bad = inputs;
            auto config = Json::parse(bad[1].bytes);
            config["skeleton"]["filename"] = "../outside.ozz";
            const auto text = config.dump();
            const auto bytes = std::as_bytes(std::span(text));
            bad[1].bytes.assign(bytes.begin(), bytes.end());
            reject(bad);
            std::stop_source stop;
            stop.request_stop();
            reject(inputs, stop.get_token());
            bool missing_tool = false;
            try {
                (void)run_model_animation_process(root / "missing-converter", root, inputs);
            } catch (const std::exception&) {
                missing_tool = true;
            }
            require(missing_tool, "Missing converter was accepted");
            require(std::filesystem::is_empty(root / ".forge/jobs"),
                    "Converter left per-run staging behind");
        } else {
            using namespace forge::animation_detail;
            const auto meta = Json::parse(read(root / "metadata.json"));
            const auto expected = Json::parse(read(root / "expected.json"));
            std::vector<ArtifactFile> files{{"skeleton.ozz", read(root / "skeleton.ozz")}};
            for (const auto& clip : meta.at("clips")) {
                const auto file = clip.at("file").get<std::string>();
                files.push_back({file, read(root / file)});
            }
            validate_model_animation(meta, files);
            for (const auto& clip : meta.at("clips")) {
                if (!clip.at("morph_tracks").empty()) {
                    auto bad = meta;
                    bad["clips"][0]["morph_tracks"][0]["node"] = 99999;
                    bool rejected = false;
                    try {
                        validate_model_animation(bad, files);
                    } catch (const std::exception&) {
                        rejected = true;
                    }
                    require(rejected, "Foreign morph node accepted");
                }
            }
            auto reject = [&](const Json& metadata, const std::vector<ArtifactFile>& input) {
                bool rejected = false;
                try {
                    validate_model_animation(metadata, input);
                } catch (const std::exception&) {
                    rejected = true;
                }
                require(rejected, "Invalid model animation candidate accepted");
            };
            if (!meta.at("skins").empty()) {
                auto bad_skin = meta;
                bad_skin["skins"][0]["joints"][0] = 99999;
                reject(bad_skin, files);
                bad_skin = meta;
                bad_skin["skins"][0]["inverse_bind_matrices"][0][15] = 0;
                reject(bad_skin, files);
                bad_skin = meta;
                bad_skin["skins"][0]["inverse_bind_matrices"] = Json::array();
                reject(bad_skin, files);
            }
            // Whole-family binding: geometry alone is deliberately unpublishable.
            const auto geometry = cook_gltf_geometry_bundle(
                NativeGltfDocument(capture_gltf_source(root, "input.gltf")));
            bool incomplete = false;
            try {
                (void)validate_model_bundle(geometry);
            } catch (const std::exception&) {
                incomplete = true;
            }
            require(incomplete, "Incomplete rig geometry was publishable");
            const auto geometry_index =
                validate_model_bundle(geometry, ModelValidation::GeometryStage);
            const Json provenance{
                {"converter", "gltf2ozz"},
                {"converter_revision", "744eb9d99f606eda849acb0b1204f7a3dc20bca1"},
                {"converter_sha256", std::string(64, 'a')},
                {"source_digest", geometry_index.source_digest}};
            const auto complete = complete_model_animation(geometry, meta, files, provenance);
            const auto complete_index = validate_model_bundle(complete);
            require(complete_index.hierarchy.at("animation").at("clips").size() ==
                        meta.at("clips").size(),
                    "Complete family lost clips");
            auto reject_bundle = [&](auto candidate) {
                bool rejected = false;
                try {
                    (void)validate_model_bundle(candidate);
                } catch (const std::exception&) {
                    rejected = true;
                }
                require(rejected, "Invalid complete model family accepted");
            };
            auto corrupt_index = [&](auto mutate) {
                auto candidate = complete;
                auto index = complete_index;
                mutate(index);
                for (auto& file : candidate)
                    if (file.name == "model.json")
                        file.bytes = encode_model_bundle_index(index);
                reject_bundle(std::move(candidate));
            };
            corrupt_index([](auto& index) {
                index.hierarchy["animation"]["provenance"]["source_digest"] = std::string(64, 'f');
            });
            corrupt_index([&](auto& index) {
                index.hierarchy["nodes"][meta["joint_nodes"][0].template get<std::size_t>()]
                               ["local"][3] = 12345.;
            });
            if (!meta.at("clips").empty())
                corrupt_index(
                    [](auto& index) { index.hierarchy["animation"]["clips"][0] = "/missing"; });
            auto missing = complete;
            missing.pop_back();
            reject_bundle(std::move(missing));
            auto bad = meta;
            bad["joint_rest_models"][0][12] = 123.;
            reject(bad, files);
            bad = meta;
            bad["joint_nodes"][0] = 99999;
            reject(bad, files);
            bad = meta;
            bad["joint_parents"][0] = 0;
            reject(bad, files);
            auto broken = files;
            broken[0].bytes.resize(4);
            reject(meta, broken);
            broken = files;
            broken.push_back(files[0]);
            reject(meta, broken);
            if (!meta.at("clips").empty()) {
                bad = meta;
                bad["clips"][0]["duration"] = 999.;
                reject(bad, files);
                bad = meta;
                bad["clips"][0]["file"] = "../outside.ozz";
                reject(bad, files);
            }
            auto skeleton = std::make_shared<Skeleton>(read(root / "skeleton.ozz"));
            std::vector<std::string> names;
            for (const auto& node : meta.at("joint_nodes"))
                names.push_back("forge_joint_" + std::to_string(node.get<std::size_t>()));
            require(skeleton->joint_names() == names, "Converter changed joint ordering");
            auto check = [&](const std::vector<Matrix>& pose, const Json& checks) {
                for (const auto& c : checks) {
                    const auto joint = c.at(0).get<std::size_t>(),
                               element = c.at(1).get<std::size_t>();
                    const auto value = c.at(2).get<float>();
                    require(std::abs(pose.at(joint).at(element) - value) <=
                                .003f * std::max(1.f, std::abs(value)),
                            "Converted pose mismatch");
                }
            };
            check(skeleton->rest_pose(), expected.at("rest"));
            require(skeleton->info().parents ==
                        expected.at("parents").get<std::vector<std::int16_t>>(),
                    "Converted parents mismatch");
            for (const auto& clip : meta.at("clips")) {
                auto animation =
                    std::make_shared<Clip>(read(root / clip.at("file").get<std::string>()));
                require(animation->info().tracks == skeleton->info().tracks &&
                            std::abs(animation->info().duration -
                                     clip.at("duration").get<float>()) < 1e-5f,
                        "Clip binding or duration mismatch");
                Sampler sampler(skeleton, animation);
                for (const auto& sample : expected.at("samples"))
                    check(sampler.sample(sample.at("ratio").get<float>()), sample.at("values"));
            }
        }
        std::cout << "glTF canonical Ozz transport verified\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
