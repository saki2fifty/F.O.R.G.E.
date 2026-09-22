#include "asset_bytes.hpp"
#include "authored_schema.hpp"
#include "bounded_json.hpp"
#include "gltf_native.hpp"
#include "model_importer.hpp"
#include "reflected_value.hpp"
#include "shader_pipeline.hpp"
#include "texture_import.hpp"
#include <forge/world.hpp>
#include <fstream>
#include <iostream>
using namespace forge;
using namespace forge::asset_detail;
using namespace forge::detail;
namespace {
constexpr std::uint32_t seed = 0x46524737;
std::uint32_t next(std::uint32_t& state) {
    state ^= state << 13;
    state ^= state >> 17;
    state ^= state << 5;
    return state;
}
void check(bool ok, const char* why) {
    if (!ok)
        throw std::runtime_error(why);
}
void write(const std::filesystem::path& path, std::span<const std::byte> bytes) {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    check(bool(out.write(reinterpret_cast<const char*>(bytes.data()),
                         std::streamsize(bytes.size()))) &&
              bool(out.flush()),
          "Corpus write failed");
}
void write_json(const std::filesystem::path& path, const Json& value) {
    const auto text = value.dump();
    write(path, std::as_bytes(std::span(text)));
}
template <class F> void reject(F run) {
    bool rejected = false;
    try {
        run();
    } catch (const std::exception&) {
        rejected = true;
    }
    check(rejected, "Malformed regression unexpectedly accepted");
}
template <class F>
void mutations(std::string_view family, std::span<const std::byte> base, F admit) {
    unsigned accepted = 0, rejected = 0;
    std::uint32_t state = seed;
    admit(base); // Valid control must still work before and after rejected candidates.
    for (unsigned i = 0; i < 128; ++i) {
        std::vector<std::byte> candidate(base.begin(), base.end());
        if (i % 4 == 0)
            candidate.resize(next(state) % candidate.size());
        else
            candidate[next(state) % candidate.size()] ^= std::byte(1u << (next(state) % 8));
        try {
            admit(candidate);
            ++accepted;
        } catch (const std::exception&) {
            ++rejected;
        }
    }
    admit(base);
    check(rejected > 0, "Mutation corpus did not exercise rejection");
    std::cout << Json{{"family", family},
                      {"seed", seed},
                      {"cases", 128},
                      {"accepted", accepted},
                      {"rejected", rejected}}
                     .dump()
              << '\n';
}
struct Fields {
    float health;
    std::uint32_t count;
};
} // namespace
int main(int argc, char** argv) {
    try {
        check(argc == 3, "Need pinned fixture root and scratch directory");
        const auto root = std::filesystem::absolute(argv[2]) / AssetId::generate().str();
        std::filesystem::create_directories(root);
        const auto glb =
            read_bytes(std::filesystem::path(argv[1]) / "Box/glTF-Binary/Box.glb", 65536);
        mutations("glb", glb, [&](auto bytes) {
            write(root / "candidate.glb", bytes);
            NativeGltfDocument model(capture_gltf_source(
                root, "candidate.glb", model_cook_extensions(), {65536, 131072, 65536, 8, 1024}));
            for (std::size_t m = 0;
                 m < model.source().document.value("meshes", Json::array()).size(); ++m)
                validate_mesh(cook_gltf_mesh(model, m));
        });
        std::vector<std::byte> tga(30, std::byte{128});
        std::fill(tga.begin(), tga.begin() + 18, std::byte{});
        tga[2] = std::byte{2};
        tga[12] = tga[14] = std::byte{2};
        tga[16] = std::byte{24};
        tga[17] = std::byte{32};
        mutations("image-header-and-payload", tga, [&](auto bytes) {
            TextureImportSettings settings;
            settings.max_size = 64;
            const auto image =
                import_texture_image(bytes, "candidate.tga", settings, {65536, 64, 1, 1, 8});
            validate_texture(image, {65536, 64, 1, 1, 8});
        });
        AssetBuildInput input;
        input.source_digest = std::string(64, 'a');
        input.importer = "forge.corpus";
        input.importer_revision = std::string(64, 'b');
        input.output_format = "corpus.bytes";
        input.platform = "portable";
        input.backend = "none";
        input.profile = "cpu";
        DerivedDataCache cache(root, {65536, 131072, 8});
        const auto valid = [&](const CachedArtifact& a) {
            check(a.files.size() == 1 && a.files[0].bytes == tga,
                  "Corrupt bytes reached artifact admission");
        };
        const auto artifact = cache.publish(input, {{"data.bin", tga}}, valid);
        const auto manifest = root / ".forge/cache/derived" / artifact.key / "manifest.json";
        const auto manifest_bytes = read_bytes(manifest, 65536);
        mutations("artifact-manifest", manifest_bytes, [&](auto bytes) {
            write(manifest, bytes);
            (void)cache.load_selected(artifact.key, valid);
        });
        for (const auto& bad_path :
             {"../outside.hlsli", "/outside.hlsli", "C:/outside.hlsli",
              "https://example.invalid/source.hlsl", "a/../../outside.hlsli"})
            reject([&] { validate_shader_sources({{bad_path, "// minimal rejected include"}}); });
        const ShaderProgramSource program{{{ShaderStage::Vertex, "main.hlsl", "main"}}};
        ShaderSnapshot snapshot;
        snapshot.document = {
            {"format", "forge.shader"},
            {"version", 1},
            {"asset_id", AssetId::generate()},
            {"source_root", "Shaders"},
            {"stages", Json::array({{{"stage", "vertex"}, {"source", "main.hlsl"}}})}};
        const auto document_text = snapshot.document.dump();
        snapshot.document_digest = content_digest(std::as_bytes(std::span(document_text)));
        snapshot.program = program;
        snapshot.sources = {{"main.hlsl", "#include \"common.hlsli\"\n"},
                            {"common.hlsli", "// known captured include"}};
        const ShaderCompilerProfile compiler{std::string(64, 'b'), false};
        const auto request = shader_process_request(snapshot, {}, compiler);
        check(decode_shader_process_request(request, compiler).sources == snapshot.sources,
              "Shader control transport changed");
        for (unsigned i = 0; i < 32; ++i) {
            auto candidate = request;
            auto& bytes = candidate.inputs.front().bytes;
            bytes[i % bytes.size()] ^= std::byte{1};
            reject([&] { decode_shader_process_request(candidate, compiler); });
        }
        check(decode_shader_process_request(request, compiler).sources == snapshot.sources,
              "Bad transport changed previous shader inputs");
        EngineContext engine(WorldRole::Validation);
        auto& world = engine.world().world();
        const auto type = world.component<Fields>("corpus.Fields")
                              .member<float>("health")
                              .member<std::uint32_t>("count");
        const Json defaults = {{"health", 100.0}, {"count", 3u}};
        opt_in_authoring(world, type, "corpus.fields", "corpus.module", 1, defaults, "Test");
        const auto schema = export_authored_types(world);
        const auto schema_text = schema.dump();
        mutations("custom-schema-transport", std::as_bytes(std::span(schema_text)),
                  [&](auto bytes) {
                      const auto copied = parse_bounded_json(bytes, 65536, 4096, 24);
                      validate_authored_types(world, copied);
                  });
        const auto value_text = defaults.dump();
        mutations("custom-value-transport", std::as_bytes(std::span(value_text)), [&](auto bytes) {
            const auto value = parse_bounded_json(bytes, 65536, 4096, 24);
            validate_reflected_json(schema[0].at("structure"), value);
            ReflectedCandidate candidate(world, type, value);
        });
        check(export_authored_types(world) == schema,
              "Rejected copied schemas changed native metadata");
        check(cache.load_selected(artifact.key, valid).files[0].bytes == tga,
              "Rejected corpus damaged immutable artifact bytes");
        std::filesystem::remove_all(root);
        std::cout << "Bounded deterministic defensive corpus passed; shader allowlist/hash rejects "
                     "included\n";
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
