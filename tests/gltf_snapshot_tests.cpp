#include "asset_bytes.hpp"
#include "gltf_native.hpp"
#include "gltf_snapshot.hpp"
#include <iostream>
#include <ranges>
using namespace forge;
using namespace forge::asset_detail;
namespace {
using Json = nlohmann::json;
using Bytes = std::vector<std::byte>;
void require(bool ok, const char* why) {
    if (!ok)
        throw std::runtime_error(why);
}
template <class F> void rejects(F fn) {
    try {
        fn();
    } catch (const std::exception&) {
        return;
    }
    throw std::runtime_error("Invalid snapshot accepted");
}
GltfSourceBundle shared() {
    GltfSourceBundle s;
    s.source = "Assets/captured.glb";
    s.source_digest = std::string(64, 'a');
    s.binary_container = true;
    s.captured_bytes = 128;
    s.document = {
        {"asset", {{"version", "2.0"}}},
        {"buffers", Json::array({{{"byteLength", 16}}})},
        {"bufferViews", Json::array({{{"buffer", 0}, {"byteOffset", 4}, {"byteLength", 4}}})},
        {"images", Json::array({{{"bufferView", 0}, {"mimeType", "image/png"}}})}};
    auto bytes = std::make_shared<Bytes>(128, std::byte{7});
    s.buffers.push_back({bytes, 48, 16});
    s.images.push_back({{bytes, 52, 4}, "image/png"});
    s.dependencies.push_back({"Assets/shared.bin", "gltf.buffer:0", std::string(64, 'b')});
    s.optional_extensions.push_back("EXAMPLE_optional");
    s.diagnostics.push_back("Source optional metadata retained");
    return s;
}
ArtifactFile& file(std::vector<ArtifactFile>& files, const std::string& name) {
    for (auto& f : files)
        if (f.name == name)
            return f;
    throw std::runtime_error("Missing fixture file");
}
void edit_index(std::vector<ArtifactFile>& files, const std::function<void(Json&)>& edit) {
    auto& bytes = file(files, "capture.json").bytes;
    auto index = Json::parse(bytes.begin(), bytes.end());
    edit(index);
    const auto text = index.dump();
    const auto view = std::as_bytes(std::span(text));
    bytes.assign(view.begin(), view.end());
}
} // namespace
int main(int argc, char** argv) {
    try {
        require(argc == 2, "Need official model fixture root");
        const auto s = shared();
        const auto encoded = encode_gltf_snapshot(s);
        require(encoded.size() == 3, "Shared GLB storage was duplicated per range");
        auto decoded = decode_gltf_snapshot(encoded);
        require(decoded.document == s.document && decoded.source == s.source &&
                    decoded.source_digest == s.source_digest &&
                    decoded.dependencies == s.dependencies &&
                    decoded.optional_extensions == s.optional_extensions &&
                    decoded.diagnostics == s.diagnostics && decoded.binary_container &&
                    decoded.captured_bytes == 128,
                "Captured provenance changed");
        require(
            decoded.images[0].encoded.storage == decoded.buffers[0].storage &&
                decoded.images[0].encoded.offset == 52 && decoded.buffers[0].offset == 48 &&
                std::ranges::equal(decoded.images[0].encoded.bytes(), s.images[0].encoded.bytes()),
            "GLB shared storage/ranges changed");
        auto repeat = encode_gltf_snapshot(decoded);
        require(repeat.size() == encoded.size(), "Snapshot serialization changed file count");
        for (std::size_t i = 0; i < repeat.size(); ++i)
            require(repeat[i].name == encoded[i].name && repeat[i].bytes == encoded[i].bytes,
                    "Snapshot roundtrip changed bytes");
        auto placeholder = shared();
        placeholder.document["extensionsRequired"] = {"EXT_meshopt_compression"};
        placeholder.buffers[0] = {nullptr, 0, 16};
        placeholder.images[0].encoded = {nullptr, 4, 4};
        const auto missing = decode_gltf_snapshot(encode_gltf_snapshot(placeholder));
        require(!missing.images[0].encoded.storage && missing.images[0].encoded.offset == 4 &&
                    missing.buffers[0].length == 16,
                "Compressed image placeholder allocated/lost its range");
        rejects([&] { missing.images[0].encoded.bytes(); });
        placeholder.document.erase("extensionsRequired");
        rejects([&] { encode_gltf_snapshot(placeholder); });
        for (const std::string name : {"source.json", "capture.json", "blob-0.bin"}) {
            auto bad = encoded;
            std::erase_if(bad, [&](const auto& f) { return f.name == name; });
            rejects([&] { decode_gltf_snapshot(bad); });
            bad = encoded;
            file(bad, name).bytes.pop_back();
            rejects([&] { decode_gltf_snapshot(bad); });
        }
        auto bad = encoded;
        bad.push_back(bad[0]);
        rejects([&] { decode_gltf_snapshot(bad); });
        bad = encoded;
        bad.push_back({"extra.bin", {std::byte{1}}});
        rejects([&] { decode_gltf_snapshot(bad); });
        const auto invalid = [&](auto change) {
            auto bad = encoded;
            edit_index(bad, change);
            rejects([&] { decode_gltf_snapshot(bad); });
        };
        invalid([](auto& j) { j["blobs"][0]["file"] = "../blob-0.bin"; });
        invalid([](auto& j) { j["buffers"][0]["blob"] = 1; });
        invalid([](auto& j) { j["buffers"][0]["length"] = 15; });
        invalid([](auto& j) { j["buffers"][0]["offset"] = -1; });
        invalid([](auto& j) { j["buffers"][0]["offset"] = 127; });
        invalid([](auto& j) { j["images"][0]["encoded"]["offset"] = 51; });
        invalid([](auto& j) { j["images"][0]["encoded"]["length"] = 3; });
        invalid([](auto& j) { j["images"][0]["encoded"]["blob"] = nullptr; });
        invalid([](auto& j) { j["dependencies"][0]["source"] = "../outside.bin"; });
        invalid([](auto& j) { j["source_digest"] = "not-a-digest"; });
        invalid([](auto& j) { j["captured_bytes"] = -1; });
        invalid([](auto& j) { j["diagnostics"] = Json::array({std::string(8193, 'x')}); });
        GltfSourceLimits small;
        small.file_bytes = small.total_bytes = small.json_bytes = 127;
        rejects([&] { encode_gltf_snapshot(s, small); });
        rejects([&] { decode_gltf_snapshot(encoded, small); });
        std::stop_source stop;
        stop.request_stop();
        rejects([&] { encode_gltf_snapshot(s, {}, stop.get_token()); });
        rejects([&] { decode_gltf_snapshot(encoded, {}, stop.get_token()); });
        const auto official = capture_gltf_source(argv[1], "NegativeScaleTest.gltf");
        const auto transported = decode_gltf_snapshot(encode_gltf_snapshot(official));
        NativeGltfDocument original(official), restored(transported);
        require(original.hierarchy().nodes.size() == restored.hierarchy().nodes.size(),
                "Native hierarchy changed");
        for (std::size_t i = 0; i < original.source().document.at("meshes").size(); ++i)
            require(encode_mesh(cook_gltf_mesh(original, i)) ==
                        encode_mesh(cook_gltf_mesh(restored, i)),
                    "Native geometry changed across immutable snapshot");
        require(transported.images.size() == official.images.size() &&
                    transported.dependencies == official.dependencies,
                "Official image/dependency transport changed");
        std::cout << "Immutable glTF snapshots, shared ranges, placeholders, digest admission and "
                     "native geometry passed\n";
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
