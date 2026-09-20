#include "asset_bytes.hpp"
#include "cooked_envelope.hpp"
#include <chrono>
#include <forge/material_resource.hpp>
#include <fstream>
#include <iostream>
#include <limits>
using namespace forge;
using namespace std::chrono_literals;
namespace {
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
    throw std::runtime_error("Invalid material accepted");
}
MaterialData material() {
    MaterialData m;
    m.model = "test.pbr.v1";
    m.alpha = MaterialAlpha::Mask;
    m.alpha_cutoff = 2; // Valid glTF cutoff; never clamp to one.
    m.double_sided = true;
    m.parameters["roughness"] = {MaterialParameterType::Scalar, {.7f, 0, 0, 0}};
    m.parameters["color"] = {MaterialParameterType::LinearColor4, {2, .1f, .5f, 1}};
    m.parameters["direction"] = {MaterialParameterType::Vector3, {-1, 0, 1, 0}};
    MaterialTextureSlot slot;
    slot.semantic = TextureSemantic::Color;
    slot.uv_set = 83;
    slot.scale = {-2, 0};
    slot.offset = {.1f, -.3f};
    slot.rotation = .5f;
    slot.sampler.u = TextureWrap::MirroredRepeat;
    m.textures["baseColor"] = slot;
    slot.semantic = TextureSemantic::Normal;
    slot.sampler.u = TextureWrap::ClampEdge;
    m.textures["normal"] = slot;
    return m;
}
MaterialLayout layout(const MaterialData& m) {
    MaterialLayout result;
    result.model = m.model;
    for (const auto& [key, value] : m.parameters)
        result.parameters[key] = value.type;
    for (const auto& [key, value] : m.textures)
        result.textures[key] = {value.semantic, value.dimension, true};
    return result;
}
std::vector<std::byte> changed(const std::vector<std::byte>& bytes,
                               const std::function<void(nlohmann::json&)>& mutate) {
    std::array<std::byte, 8> magic;
    std::copy_n(bytes.begin(), 8, magic.begin());
    auto envelope = asset_detail::decode_envelope(bytes, magic, 1024 * 1024, 0);
    mutate(envelope.metadata);
    return asset_detail::encode_envelope(envelope.metadata, {}, magic, 1024 * 1024);
}
} // namespace
int main(int argc, char** argv) {
    try {
        require(argc == 2, "Need material scratch directory");
        const auto m = material();
        const auto bytes = encode_material(m);
        const auto decoded = decode_material(bytes);
        require(decoded == m && encode_material(decoded) == bytes,
                "Material roundtrip changed values");
        require(decoded.textures.at("normal").semantic == TextureSemantic::Normal &&
                    decoded.textures.at("baseColor").sampler.u == TextureWrap::MirroredRepeat &&
                    decoded.textures.at("baseColor").scale[1] == 0 &&
                    decoded.parameters.at("color").type == MaterialParameterType::LinearColor4,
                "Material semantics/signed UV transform lost");
        for (std::size_t size = 0; size < bytes.size(); ++size)
            rejects([&] { decode_material(std::span(bytes).first(size)); });
        for (unsigned at : {0u, 8u, 12u, 16u, 20u}) {
            auto bad = bytes;
            bad[at] = std::byte{255};
            rejects([&] { decode_material(bad); });
        }
        auto bad_bytes = bytes;
        bad_bytes.push_back(std::byte{0});
        rejects([&] { decode_material(bad_bytes); });
        for (const auto& field : {"alpha", "parameters", "textures"}) {
            auto invalid = changed(bytes, [&](auto& j) { j[field] = -1; });
            rejects([&] { decode_material(invalid); });
        }
        for (const auto& field : {"type", "value"}) {
            auto invalid = changed(bytes, [&](auto& j) { j["parameters"]["color"][field] = 256; });
            rejects([&] { decode_material(invalid); });
        }
        for (const auto& field : {"semantic", "dimension", "uv_set"}) {
            auto invalid = changed(bytes, [&](auto& j) { j["textures"]["normal"][field] = -1; });
            rejects([&] { decode_material(invalid); });
        }
        for (auto value : {1e100, 1e-100}) {
            auto invalid =
                changed(bytes, [&](auto& j) { j["parameters"]["roughness"]["value"][0] = value; });
            rejects([&] { decode_material(invalid); });
        }
        auto bad = m;
        bad.parameters["roughness"].value[1] = 1;
        rejects([&] { validate_material(bad); });
        bad = m;
        bad.parameters["color"].value[0] = std::numeric_limits<float>::infinity();
        rejects([&] { encode_material(bad); });
        bad = m;
        bad.parameters["invalid/key"] = {};
        rejects([&] { validate_material(bad); });
        bad = m;
        bad.textures["color"] = {};
        rejects([&] { validate_material(bad); });
        bad = m;
        bad.textures.at("normal").sampler.anisotropy = 0;
        rejects([&] { encode_material(bad); });
        bad = m;
        for (unsigned i = 0; i < 257; ++i)
            bad.parameters["extra" + std::to_string(i)] = {};
        rejects([&] { encode_material(bad); });
        bad = m;
        bad.parameters.clear();
        for (unsigned i = 0; i < 65; ++i)
            bad.textures["extra" + std::to_string(i)] = {};
        rejects([&] { encode_material(bad); });
        auto reflected = layout(m);
        validate_material_layout(m, reflected);
        reflected.parameters.at("color") = MaterialParameterType::Vector4;
        rejects([&] { validate_material_layout(m, reflected); });
        reflected = layout(m);
        reflected.textures.at("normal").dimension = TextureDimension::Cube;
        rejects([&] { validate_material_layout(m, reflected); });
        reflected = layout(m);
        reflected.textures.at("normal").semantic = TextureSemantic::Color;
        rejects([&] { validate_material_layout(m, reflected); });
        reflected = layout(m);
        reflected.textures["extra"] = {TextureSemantic::Data, TextureDimension::D2, true};
        rejects([&] { validate_material_layout(m, reflected); });
        reflected.textures.at("extra").required = false;
        validate_material_layout(m, reflected);
        reflected = layout(m);
        reflected.model = "another.model";
        rejects([&] { validate_material_layout(m, reflected); });
        const AssetRef<TextureAsset> texture{AssetId::generate()};
        MaterialTextureBindings bindings{{"baseColor", texture}, {"normal", texture}};
        validate_material_bindings(m, bindings); // One image, distinct binding semantics.
        auto missing = bindings;
        missing.erase("normal");
        rejects([&] { validate_material_bindings(m, missing); });
        missing = bindings;
        missing["normal"] = {};
        rejects([&] { validate_material_bindings(m, missing); });
        missing = bindings;
        missing["unknown"] = texture;
        rejects([&] { validate_material_bindings(m, missing); });
        const std::filesystem::path root = argv[1];
        std::filesystem::create_directories(root);
        const auto file = root / "material.fmat";
        {
            std::ofstream out(file, std::ios::binary);
            out.write(reinterpret_cast<const char*>(bytes.data()), std::streamsize(bytes.size()));
        }
        const auto digest = asset_detail::content_digest(bytes);
        const AssetRef<MaterialAsset> id{AssetId::generate()};
        ResourcePool<MaterialAsset> pool;
        auto first = pool.request(id, digest, 1,
                                  material_resource_loader(file, digest, bindings, layout(m)));
        require(pool.wait(first, 5s), "Material resource load failed");
        auto lease = pool.acquire(first);
        require(lease && lease->textures == bindings, "Material binding selection lost");
        auto new_bindings = bindings;
        new_bindings.at("normal") = {AssetId::generate()};
        auto second = pool.request(id, digest, 2,
                                   material_resource_loader(file, digest, new_bindings, layout(m)));
        require(pool.wait(second, 5s) && pool.acquire(second)->textures == new_bindings &&
                    lease->textures == bindings,
                "Same cooked bytes/new publication did not replace bindings safely");
        require(first.inspect().identity != second.inspect().identity,
                "Material generations coalesced incorrectly");
        rejects([&] {
            pool.request(id, digest, 1,
                         material_resource_loader(file, digest, bindings, layout(m)));
        });
        auto broken_layout = layout(m);
        broken_layout.parameters.erase("roughness");
        auto failed = pool.request(
            id, digest, 3, material_resource_loader(file, digest, new_bindings, broken_layout));
        require(!pool.wait(failed, 5s) && pool.current(id).identity() == second.inspect().identity,
                "Incompatible layout replaced last-good material");
        failed = pool.request(
            id, digest, 4,
            material_resource_loader(file, std::string(64, 'f'), new_bindings, layout(m)));
        require(!pool.wait(failed, 5s) && pool.current(id).identity() == second.inspect().identity,
                "Corrupt material replaced last-good selection");
        auto cancelled = material_resource_loader(file, digest, bindings, layout(m));
        std::stop_source stop;
        stop.request_stop();
        rejects([&] { cancelled(stop.get_token()); });
        std::cout << "Cooked materials, binding semantics, layout mismatch, resource generations "
                     "and last-good retention passed\n";
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
