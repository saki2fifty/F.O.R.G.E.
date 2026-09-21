#include "gltf_container.hpp"
#include <forge/gltf_source.hpp>
#include <forge/scene.hpp>
#include <fstream>
#include <iostream>

using namespace forge;
namespace {
using Json = nlohmann::json;
using Bytes = std::vector<std::byte>;
void require(bool value, const char* message) {
    if (!value)
        throw std::runtime_error(message);
}
template <class F> void rejects(F fn, std::string_view expected = {}) {
    try {
        fn();
    } catch (const std::exception& error) {
        if (std::string_view(error.what()).find(expected) == std::string_view::npos)
            throw std::runtime_error("Unexpected admission diagnostic: " +
                                     std::string(error.what()));
        return;
    }
    throw std::runtime_error("Invalid glTF source accepted");
}
void write(const std::filesystem::path& file, const Bytes& bytes) {
    std::ofstream stream(file, std::ios::binary);
    stream.write(reinterpret_cast<const char*>(bytes.data()),
                 static_cast<std::streamsize>(bytes.size()));
    if (!stream)
        throw std::runtime_error("Fixture write failed");
}
void append(Bytes& bytes, std::uint32_t value) {
    for (unsigned i = 0; i < 4; ++i)
        bytes.push_back(std::byte((value >> (8 * i)) & 255));
}
Bytes glb(Json json, Bytes bin, bool unknown = false) {
    auto encoded = json.dump();
    while (encoded.size() % 4)
        encoded += ' ';
    while (bin.size() % 4)
        bin.push_back(std::byte{0});
    Bytes result;
    append(result, 0x46546c67);
    append(result, 2);
    append(result,
           static_cast<std::uint32_t>(28 + encoded.size() + bin.size() + (unknown ? 12 : 0)));
    append(result, static_cast<std::uint32_t>(encoded.size()));
    append(result, 0x4e4f534a);
    for (unsigned char c : encoded)
        result.push_back(std::byte(c));
    append(result, static_cast<std::uint32_t>(bin.size()));
    append(result, 0x004e4942);
    result.insert(result.end(), bin.begin(), bin.end());
    if (unknown) {
        append(result, 4);
        append(result, 0x12345678);
        append(result, 0);
    }
    return result;
}
} // namespace
int main(int argc, char** argv) {
    try {
        require(argc == 2, "Expected scratch directory");
        const auto root = std::filesystem::absolute(argv[1]) / AssetId::generate().str();
        std::filesystem::create_directories(root / "Assets/model");
        struct Cleanup {
            std::filesystem::path root;
            ~Cleanup() {
                std::error_code error;
                std::filesystem::remove_all(root, error);
            }
        } cleanup{root};
        const auto source = std::filesystem::path("Assets/model/test.gltf");
        auto capture = [&](Json document) {
            atomic_write(root / source, document.dump());
            return capture_gltf_source(root, source);
        };
        const Json base{{"asset", {{"version", "2.0"}}}};
        auto empty = capture(base);
        require(empty.buffers.empty() && empty.images.empty() && !empty.binary_container,
                "Empty core source not admitted");
        Bytes four{std::byte{0}, std::byte{1}, std::byte{2}, std::byte{3}};
        write(root / "Assets/model/buffer data.bin", four);
        write(root / "Assets/image.png", four); // Capture only: not a valid decoded PNG.
        auto doc = base;
        doc["buffers"] = Json::array({{{"byteLength", 4}, {"uri", "buffer%20data.bin"}},
                                      {{"byteLength", 4}, {"uri", "buffer%20data.bin"}}});
        doc["bufferViews"] = Json::array({{{"buffer", 0}, {"byteOffset", 1}, {"byteLength", 2}}});
        doc["images"] = Json::array(
            {{{"uri", "../image.png"}}, {{"bufferView", 0}, {"mimeType", "image/png"}}});
        auto external = capture(doc);
        require(external.buffers.size() == 2 && external.images.size() == 2 &&
                    external.dependencies.size() == 3 &&
                    external.images[1].encoded.bytes().size() == 2,
                "External sources or embedded image ranges not captured");
        require(external.buffers[0].storage == external.buffers[1].storage &&
                    external.captured_bytes == doc.dump().size() + 8,
                "Repeated dependency copied or counted twice");
        {
            std::filesystem::create_directories(root / "Moved");
            auto authored = doc;
            authored["extras"] = {{"uri", "../image.png"}};
            const auto encoded = authored.dump(2);
            const auto input = std::as_bytes(std::span(encoded));
            const ProjectPaths paths(root);
            const auto moved =
                gltf_detail::relocate_gltf_source(paths, source, "Moved/model.gltf", input);
            const auto edited = Json::parse(moved);
            require(edited["buffers"][0]["uri"] == "../Assets/model/buffer%20data.bin" &&
                        edited["images"][0]["uri"] == "../Assets/image.png" &&
                        edited["extras"] == authored["extras"],
                    "glTF relocation lost resource locators or changed opaque data");
            atomic_write(root / "Moved/model.gltf", moved);
            const auto adopted = capture_gltf_source(root, "Moved/model.gltf");
            require(adopted.dependencies == external.dependencies &&
                        std::equal(adopted.buffers[0].bytes().begin(),
                                   adopted.buffers[0].bytes().end(),
                                   external.buffers[0].bytes().begin()),
                    "Relocated glTF did not capture identical external resources");
            require(gltf_detail::relocate_gltf_source(paths, source, "Assets/model/renamed.gltf",
                                                      input) == encoded,
                    "Same-folder rename unnecessarily rewrote source bytes");
            const auto at_root =
                gltf_detail::relocate_gltf_source(paths, source, "root.gltf", input);
            require(Json::parse(at_root)["images"][0]["uri"] == "Assets/image.png",
                    "Project-root destination broke relative URI rebasing");
            rejects([&] {
                (void)gltf_detail::relocate_gltf_source(paths, source, "../escape.gltf", input);
            });
            auto unsafe = authored;
            unsafe["buffers"][0]["uri"] = "../../../outside.bin";
            const auto unsafe_text = unsafe.dump();
            rejects([&] {
                (void)gltf_detail::relocate_gltf_source(paths, source, "Moved/model.gltf",
                                                        std::as_bytes(std::span(unsafe_text)));
            });
        }
        write(root / "Assets/model/buffer data.bin", Bytes{std::byte{9}});
        require(external.buffers[0].bytes()[1] == std::byte{1},
                "Captured source changed after external overwrite");
        write(root / "Assets/model/buffer data.bin", four);
        auto data = base;
        data["buffers"] = Json::array(
            {{{"byteLength", 4}, {"uri", "data:application/octet-stream;base64,AAECAw=="}}});
        auto embedded = capture(data);
        require(embedded.buffers[0].bytes().size() == 4 &&
                    embedded.buffers[0].bytes()[3] == std::byte{3} && embedded.dependencies.empty(),
                "Base64 embedded buffer decoded incorrectly");
        auto percent_data = data;
        percent_data["buffers"][0]["uri"] = "data:application/octet-stream,%00%01%02%03";
        require(capture(percent_data).buffers[0].bytes()[3] == std::byte{3},
                "Percent-encoded binary data URI decoded incorrectly");
        auto binary_doc = base;
        binary_doc["buffers"] = Json::array({{{"byteLength", 3}}});
        const auto binary = glb(binary_doc, Bytes{std::byte{0}, std::byte{1}, std::byte{2}}, true);
        write(root / "Assets/model/BINARY.GLB", binary);
        auto captured_binary = capture_gltf_source(root, "Assets/model/BINARY.GLB");
        require(captured_binary.binary_container && captured_binary.buffers[0].length == 3 &&
                    captured_binary.diagnostics.size() == 1,
                "GLB source/chunk/padding admission failed");
        {
            auto document = binary_doc;
            document["images"] = Json::array({{{"uri", "../image.png"}}});
            document["extras"] = {{"uri", "../image.png"}};
            const auto original =
                glb(document, Bytes{std::byte{0}, std::byte{1}, std::byte{2}}, true);
            const auto moved = gltf_detail::relocate_gltf_source(
                ProjectPaths(root), "Assets/model/source.glb", "Moved/source.glb", original);
            const auto prior = gltf_detail::gltf_container(original);
            const auto next = gltf_detail::gltf_container(std::as_bytes(std::span(moved)));
            require(next.chunks.size() == prior.chunks.size() && next.json.size() % 4 == 0 &&
                        Json::parse(next.json)["extras"] == document["extras"],
                    "GLB relocation changed chunk set, alignment or opaque JSON");
            for (std::size_t i = 1; i < prior.chunks.size(); ++i)
                require(prior.chunks[i].kind == next.chunks[i].kind &&
                            std::equal(prior.chunks[i].bytes.begin(), prior.chunks[i].bytes.end(),
                                       next.chunks[i].bytes.begin(), next.chunks[i].bytes.end()),
                        "GLB relocation changed BIN or unknown chunk bytes");
            atomic_write(root / "Moved/source.glb", moved);
            require(capture_gltf_source(root, "Moved/source.glb").buffers[0].bytes()[2] ==
                        std::byte{2},
                    "Relocated GLB no longer passed normal source admission");
        }
        // Every truncated container must fail rather than reaching a native loader.
        for (std::size_t size = 0; size < binary.size(); ++size) {
            write(root / "Assets/model/truncated.glb",
                  Bytes(binary.begin(), binary.begin() + size));
            rejects([&] { (void)capture_gltf_source(root, "Assets/model/truncated.glb"); });
        }
        auto bad_binary = binary;
        bad_binary[4] = std::byte{1};
        write(root / "Assets/model/bad.glb", bad_binary);
        rejects([&] { (void)capture_gltf_source(root, "Assets/model/bad.glb"); }, "version");
        bad_binary = binary;
        bad_binary[binary.size() - 13] = std::byte{1}; // Last BIN padding byte.
        write(root / "Assets/model/bad.glb", bad_binary);
        rejects([&] { (void)capture_gltf_source(root, "Assets/model/bad.glb"); }, "padding");
        for (const auto* uri : {"http://example.invalid/a.bin", "file:///a.bin", "//server/a.bin",
                                "../../../../outside.bin", "%2fetc/passwd", "a%00.bin", "a%5cb.bin",
                                "a?query", "a#fragment", "a%ff.bin", "a%xy.bin", "C%3a/a.bin"}) {
            auto bad = doc;
            bad["buffers"][0]["uri"] = uri;
            rejects([&] { (void)capture(bad); });
        }
        for (const auto* uri :
             {"data:application/octet-stream;base64,AAECAw=",
              "data:application/octet-stream;base64,AB==",
              "data:application/octet-stream;base64,AA=A", "data:image/png;base64,AAAA",
              "data:application/octet-stream;base64,@@@@",
              "data:application/octet-stream;base64,AA==AAAA"}) {
            auto bad = data;
            bad["buffers"][0]["uri"] = uri;
            rejects([&] { (void)capture(bad); });
        }
        auto bad = doc;
        bad["buffers"][0]["byteLength"] = 100;
        rejects([&] { (void)capture(bad); }, "truncated");
        for (auto index : {Json(-1), Json(true), Json(0.0), Json(999), Json(UINT64_MAX)}) {
            bad = doc;
            bad["bufferViews"][0]["buffer"] = index;
            rejects([&] { (void)capture(bad); });
        }
        bad = doc;
        bad["bufferViews"][0]["byteOffset"] = UINT64_MAX;
        rejects([&] { (void)capture(bad); }, "outside");
        bad = doc;
        bad["images"][0]["bufferView"] = 0;
        rejects([&] { (void)capture(bad); }, "exactly one");
        bad = base;
        bad["extensionsUsed"] = Json::array({"KHR_fixture_unknown"});
        auto optional = capture(bad);
        require(optional.optional_extensions.size() == 1, "Optional extension report lost");
        bad["extensionsRequired"] = bad["extensionsUsed"];
        rejects([&] { (void)capture(bad); }, "Unsupported required");
        // This tests admission-profile plumbing, not actual extension implementation.
        atomic_write(root / source, bad.dump());
        (void)capture_gltf_source(root, source, {"KHR_fixture_unknown"});
        bad.erase("extensionsUsed");
        rejects([&] { (void)capture(bad); }, "not declared used");
        atomic_write(root / source, R"({"asset":{"version":"2.0","version":"1.0"}})");
        rejects([&] { (void)capture_gltf_source(root, source); }, "Duplicate field");
        atomic_write(root / source, doc.dump());
        GltfSourceLimits limits;
        limits.json_bytes = 16;
        rejects([&] { (void)capture_gltf_source(root, source, {}, limits); }, "JSON exceeds");
        limits = {};
        limits.source_files = 1;
        rejects([&] { (void)capture_gltf_source(root, source, {}, limits); }, "file count");
        limits = {};
        limits.array_entries = 1;
        rejects([&] { (void)capture_gltf_source(root, source, {}, limits); }, "array exceeds");
        std::stop_source stop;
        stop.request_stop();
        rejects([&] { (void)capture_gltf_source(root, source, {}, {}, stop.get_token()); },
                "cancelled");
        std::cout << "glTF/GLB bounded capture, dependency snapshots, URI policy and malformed "
                     "input passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
