#include "asset_bytes.hpp"
#include "editor/content_model.hpp"
#include "gltf_native.hpp"
#include "gltf_scene.hpp"
#include "gltf_surfaces.hpp"
#include "model_importer.hpp"
#include "texture_import.hpp"
#include <bit>
#include <chrono>
#include <forge/texture_resource.hpp>
#include <fstream>
#include <iostream>
using namespace forge;
using namespace forge::asset_detail;
using namespace std::chrono_literals;
using Clock = std::chrono::steady_clock;
using Json = nlohmann::json;
namespace {
void check(bool value, const char* why) {
    if (!value)
        throw std::runtime_error(why);
}
template <class F> double measure(F run) {
    const auto start = Clock::now();
    run();
    return std::chrono::duration<double, std::milli>(Clock::now() - start).count();
}
void write(const std::filesystem::path& path, std::span<const std::byte> data) {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream file(path, std::ios::binary);
    check(bool(file.write(reinterpret_cast<const char*>(data.data()),
                          std::streamsize(data.size()))) &&
              bool(file.flush()),
          "Scale fixture write failed");
}
void write(const std::filesystem::path& path, const std::string& text) {
    write(path, std::as_bytes(std::span(text)));
}
void sources(const std::filesystem::path& root) {
    unsigned made = 0;
    for (unsigned count : {1000u, 10000u}) {
        for (; made < count; ++made)
            write(root / "Assets" / ("source-" + std::to_string(made) + ".png"), "source bytes");
        SourceSnapshot snapshot;
        Json result{{"workload", "source-catalog-content"}, {"assets", count}};
        result["initial_scan_ms"] = measure([&] { snapshot = scan_asset_sources(root); });
        check(snapshot.complete && snapshot.files.size() == count, "Scale scan incomplete");
        SourceChangeTracker tracker(snapshot, 0ms);
        write(root / "Assets/source-0.png", "changed data");
        std::vector<SourceChange> changes;
        result["one_change_full_rescan_ms"] = measure([&] {
            snapshot = scan_asset_sources(root);
            auto now = Clock::now();
            tracker.observe(snapshot, now);
            changes = tracker.drain(now);
        });
        check(changes.size() == 1 && changes.front().kind == SourceChangeKind::Modified,
              "Incremental scan lost change");
        std::vector<AssetRecord> records;
        for (const auto& [path, source] : snapshot.files)
            records.push_back({AssetId::generate(), "texture", path, 1, {}});
        AssetCatalog catalog(root);
        result["catalog_admit_ms"] = measure([&] { catalog.replace_all(records); });
        Json document;
        result["catalog_serialize_ms"] = measure([&] { document = catalog.document(); });
        result["catalog_json_bytes"] = document.dump().size();
        result["catalog_restore_ms"] = measure([&] {
            AssetCatalog copy(root);
            copy.restore(document);
            check(copy.records().size() == count, "Catalog scale roundtrip lost rows");
        });
        ContentIndex index;
        result["content_index_ms"] =
            measure([&] { index = ContentIndex::build(catalog, &snapshot); });
        ContentQuery query;
        query.text = "source .png";
        query.type = "texture";
        result["content_query_100_ms"] = measure([&] {
            for (unsigned i = 0; i < 100; ++i)
                check(index.query(query).size() == count, "Scale content filter lost rows");
        });
        result["source_bytes_hashed"] = snapshot.bytes_read;
        std::cout << result.dump() << '\n';
        write(root / "Assets/source-0.png", "source bytes");
    }
}
void texture_resources(const std::filesystem::path& root) {
    constexpr unsigned dimension = 2048;
    std::vector<std::byte> tga(18 + std::size_t(dimension) * dimension * 3, std::byte{128});
    std::fill(tga.begin(), tga.begin() + 18, std::byte{});
    tga[2] = std::byte{2};
    tga[13] = tga[15] = std::byte{dimension >> 8};
    tga[16] = std::byte{24};
    tga[17] = std::byte{32};
    TextureData texture;
    Json result{
        {"workload", "large-texture-cache-resource"}, {"width", dimension}, {"height", dimension}};
    result["import_mips_ms"] =
        measure([&] { texture = import_texture_image(tga, "large.tga", {}); });
    check(texture.width == dimension && texture.mips == 12, "Large texture mip chain lost");
    std::vector<std::byte> cooked;
    result["encode_ms"] = measure([&] { cooked = encode_texture(texture); });
    result["source_bytes"] = tga.size();
    result["cooked_bytes"] = cooked.size();
    write(root / "texture.bin", cooked);
    const auto digest = content_digest(cooked);
    AssetBuildInput input;
    input.source_digest = content_digest(tga);
    input.importer = "forge.scale.fixture";
    input.importer_revision = std::string(64, 'a');
    input.output_format = "texture";
    input.platform = "portable";
    input.backend = "none";
    input.profile = "cpu";
    DerivedDataCache cache(root);
    const auto valid = [&](const CachedArtifact& artifact) {
        check(artifact.files.size() == 1 && content_digest(artifact.files.front().bytes) == digest,
              "DDC scale payload differs");
    };
    result["ddc_miss_ms"] =
        measure([&] { check(!cache.find(input, valid), "Unexpected scale cache hit"); });
    result["ddc_publish_ms"] =
        measure([&] { cache.publish(input, {{"texture.bin", cooked}}, valid); });
    result["ddc_verified_hit_ms"] = measure(
        [&] { check(cache.find(input, valid).has_value(), "Scale cache miss after publish"); });
    ResourcePool<TextureAsset> pool;
    const AssetRef<TextureAsset> id{AssetId::generate()};
    ResourceTicket first;
    result["async_load_and_adopt_ms"] = measure([&] {
        first = pool.request(id, std::string(64, 'a'), 1,
                             texture_resource_loader(root / "texture.bin", digest));
        check(pool.wait(first, 30s), "Large texture resource load failed");
    });
    auto retained = pool.acquire(first);
    result["hot_replace_and_adopt_ms"] = measure([&] {
        const auto replacement = pool.request(
            id, std::string(64, 'b'), 2, texture_resource_loader(root / "texture.bin", digest));
        check(pool.wait(replacement, 30s), "Large texture resource replacement failed");
    });
    check(retained && pool.statistics().retired == 1, "Replacement invalidated retained texture");
    result["resource_peak_bytes"] = pool.statistics().high_water_bytes;
    retained = {};
    pool.collect();
    pool.unload(id);
    pool.collect();
    check(pool.statistics().memory.total() == 0, "Scale texture unload retained payload");
    std::cout << result.dump() << '\n';
}
void model_scale(const std::filesystem::path& root, const std::filesystem::path& fixtures) {
    Json source{
        {"asset", {{"version", "2.0"}}}, {"scene", 0}, {"extensionsUsed", {"KHR_lights_punctual"}}};
    for (const auto* key : {"bufferViews", "accessors", "meshes", "nodes", "materials", "images",
                            "textures", "animations"})
        source[key] = Json::array();
    std::vector<std::byte> buffer;
    auto put = [&](std::uint32_t value) {
        for (unsigned i = 0; i < 4; ++i)
            buffer.push_back(std::byte(value >> (i * 8)));
    };
    auto floats = [&](const std::vector<float>& values, unsigned components) {
        const auto offset = buffer.size();
        for (float v : values)
            put(std::bit_cast<std::uint32_t>(v));
        const auto view = source["bufferViews"].size();
        source["bufferViews"].push_back(
            {{"buffer", 0}, {"byteOffset", offset}, {"byteLength", buffer.size() - offset}});
        const auto accessor = source["accessors"].size();
        source["accessors"].push_back({{"bufferView", view},
                                       {"componentType", 5126},
                                       {"count", values.size() / components},
                                       {"type", components == 1   ? "SCALAR"
                                                : components == 2 ? "VEC2"
                                                                  : "VEC3"}});
        return accessor;
    };
    constexpr unsigned side = 256, parts = 64, nodes = 4096, clips = 64;
    std::vector<float> positions, normals, uvs;
    for (unsigned z = 0; z < side; ++z)
        for (unsigned x = 0; x < side; ++x) {
            positions.insert(positions.end(), {float(x), 0, float(z)});
            normals.insert(normals.end(), {0, 1, 0});
            uvs.insert(uvs.end(), {float(x) / (side - 1), float(z) / (side - 1)});
        }
    const auto pos = floats(positions, 3), normal = floats(normals, 3), uv = floats(uvs, 2);
    source["accessors"][pos]["min"] = {0, 0, 0};
    source["accessors"][pos]["max"] = {side - 1, 0, side - 1};
    const auto offset = buffer.size();
    unsigned index_count = 0;
    for (unsigned z = 0; z < side - 1; ++z)
        for (unsigned x = 0; x < side - 1; ++x) {
            const auto a = z * side + x;
            for (auto v : {a, a + side, a + 1, a + 1, a + side, a + side + 1}) {
                put(v);
                ++index_count;
            }
        }
    const auto index_view = source["bufferViews"].size();
    source["bufferViews"].push_back(
        {{"buffer", 0}, {"byteOffset", offset}, {"byteLength", buffer.size() - offset}});
    const auto large_indices = source["accessors"].size();
    source["accessors"].push_back({{"bufferView", index_view},
                                   {"componentType", 5125},
                                   {"count", index_count},
                                   {"type", "SCALAR"}});
    const auto small_pos = floats({0, 0, 0, 0, 0, 1, 1, 0, 0}, 3),
               small_normal = floats({0, 1, 0, 0, 1, 0, 0, 1, 0}, 3),
               small_uv = floats({0, 0, 0, 1, 1, 0}, 2);
    source["accessors"][small_pos]["min"] = {0, 0, 0};
    source["accessors"][small_pos]["max"] = {1, 0, 1};
    const auto small_offset = buffer.size();
    put(0);
    put(1);
    put(2);
    const auto small_view = source["bufferViews"].size();
    source["bufferViews"].push_back(
        {{"buffer", 0}, {"byteOffset", small_offset}, {"byteLength", 12}});
    const auto small_indices = source["accessors"].size();
    source["accessors"].push_back(
        {{"bufferView", small_view}, {"componentType", 5125}, {"count", 3}, {"type", "SCALAR"}});
    const auto times = floats({0, 1}, 1), translations = floats({0, 0, 0, 1, 0, 0}, 3);
    source["accessors"][times]["min"] = {0};
    source["accessors"][times]["max"] = {1};
    Json primitives = Json::array();
    // CC0 normal-map test art has no logo/mark restrictions on reuse in this
    // generated model. The untouched original fixture retains its attribution.
    const auto image = read_bytes(
        fixtures / "NormalTangentTest/glTF/NormalTangentTest_BaseColor.png", 1024 * 1024);
    for (unsigned p = 0; p < parts; ++p) {
        const auto name = "image-" + std::to_string(p) + ".png";
        write(root / name, image);
        source["images"].push_back({{"uri", name}});
        source["textures"].push_back({{"source", p}});
        source["materials"].push_back(
            {{"name", "Material-" + std::to_string(p)},
             {"pbrMetallicRoughness", {{"baseColorTexture", {{"index", p}}}}}});
        primitives.push_back({{"attributes",
                               {{"POSITION", p == 0 ? pos : small_pos},
                                {"NORMAL", p == 0 ? normal : small_normal},
                                {"TEXCOORD_0", p == 0 ? uv : small_uv}}},
                              {"indices", p == 0 ? large_indices : small_indices},
                              {"material", p}});
    }
    source["meshes"].push_back({{"name", "Large multi-material grid"}, {"primitives", primitives}});
    source["extensions"]["KHR_lights_punctual"]["lights"] = Json::array();
    Json roots = Json::array();
    for (unsigned n = 0; n < nodes; ++n) {
        Json node{{"name", "Node-" + std::to_string(n)}, {"mesh", 0}};
        if (n < 63)
            node["children"] = {n + 1};
        if (n == 0 || n >= 64)
            roots.push_back(n);
        if (n < 64) {
            source["extensions"]["KHR_lights_punctual"]["lights"].push_back(
                {{"type", "point"}, {"intensity", 1}, {"range", 20}});
            node["extensions"]["KHR_lights_punctual"] = {{"light", n}};
        }
        source["nodes"].push_back(node);
    }
    for (unsigned a = 0; a < clips; ++a)
        source["animations"].push_back(
            {{"name", "Clip-" + std::to_string(a)},
             {"samplers",
              Json::array(
                  {{{"input", times}, {"output", translations}, {"interpolation", "LINEAR"}}})},
             {"channels", Json::array({{{"sampler", 0},
                                        {"target", {{"node", a}, {"path", "translation"}}}}})}});
    source["scenes"] = Json::array({{{"nodes", roots}}});
    source["buffers"] = Json::array({{{"uri", "grid.bin"}, {"byteLength", buffer.size()}}});
    write(root / "grid.bin", buffer);
    write(root / "grid.gltf", source.dump());
    Json result{{"workload", "substantial-model"},
                {"large_primitive_vertices", side * side},
                {"primitives", parts},
                {"materials", parts},
                {"textures", parts},
                {"nodes", nodes},
                {"hierarchy_depth", 64},
                {"animation_clips", clips},
                {"lights", 64},
                {"index_count_large_primitive", index_count},
                {"buffer_bytes", buffer.size()}};
    std::unique_ptr<NativeGltfDocument> model;
    result["capture_admit_native_ms"] = measure([&] {
        model = std::make_unique<NativeGltfDocument>(
            capture_gltf_source(root, "grid.gltf", model_cook_extensions()));
    });
    check(model->hierarchy().nodes.size() == nodes && model->encoded_images().size() == parts,
          "Large model lost hierarchy/images");
    MeshData mesh;
    result["mesh_cook_ms"] = measure([&] { mesh = cook_gltf_mesh(*model, 0); });
    check(mesh.lods[0].parts.size() == parts && mesh.lods[0].parts[0].vertices == side * side,
          "Large mesh lost primitive data");
    std::vector<std::byte> cooked;
    result["mesh_encode_ms"] = measure([&] { cooked = encode_mesh(mesh); });
    result["mesh_cooked_bytes"] = cooked.size();
    result["mesh_decode_ms"] = measure([&] {
        check(decode_mesh(cooked).lods[0].parts.size() == parts,
              "Large mesh cooked roundtrip failed");
    });
    result["materials_images_ms"] = measure([&] {
        for (unsigned p = 0; p < parts; ++p) {
            const auto material = cook_gltf_material(*model, p);
            check(material.textures.size() == 1, "Large material lost texture");
            const auto texture =
                import_texture_image(model->encoded_images()[p].encoded.bytes(), "source.png", {});
            check(texture.width && texture.height, "Model image decode failed");
        }
    });
    result["animation_clips_ms"] = measure([&] {
        for (unsigned a = 0; a < clips; ++a)
            check(model->animation(a).tracks.size() == 1, "Large animation list lost track");
    });
    check(gltf_scene_metadata(model->scene_source()).lights.size() == 64,
          "Large model lost light definitions");
    std::cout << result.dump() << '\n';
}
} // namespace
int main(int argc, char** argv) {
    try {
        check(argc == 3, "Need official fixture root and scratch directory");
        const auto root = std::filesystem::absolute(argv[2]) / AssetId::generate().str();
        std::filesystem::create_directories(root);
        sources(root / "catalog");
        texture_resources(root / "texture");
        model_scale(root / "model", std::filesystem::absolute(argv[1]));
        std::filesystem::remove_all(root);
        std::cout << "Bounded scale characterization passed; timings are workload measurements, "
                     "not universal performance guarantees\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
