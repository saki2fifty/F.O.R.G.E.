#include "asset_bytes.hpp"
#include <bit>
#include <chrono>
#include <forge/texture_resource.hpp>
#include <fstream>
#include <iostream>
#include <limits>
#include <nlohmann/json.hpp>
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
    throw std::runtime_error("Invalid texture accepted");
}
TextureData texture(TextureFormat f = TextureFormat::RGBA8,
                    TextureDimension d = TextureDimension::D2) {
    TextureData t;
    t.width = 8;
    t.height = 8;
    t.mips = 4;
    t.format = f;
    t.dimension = d;
    if (d == TextureDimension::D2Array || d == TextureDimension::CubeArray)
        t.layers = 3;
    if (d == TextureDimension::D3)
        t.depth = 8;
    if (f == TextureFormat::BC6Unsigned || f == TextureFormat::BC6Signed)
        t.semantic = TextureSemantic::HdrColor;
    const auto n =
        t.layers * ((d == TextureDimension::Cube || d == TextureDimension::CubeArray) ? 6 : 1);
    for (unsigned layer = 0; layer < n; ++layer)
        for (unsigned mip = 0; mip < t.mips; ++mip)
            t.subresources.emplace_back(texture_layout(t, mip).bytes, std::byte{0});
    return t;
}
std::vector<std::byte> rewrite(const std::vector<std::byte>& input,
                               std::function<void(nlohmann::json&)> change) {
    unsigned n = 0;
    for (unsigned i = 0; i < 4; ++i)
        n |= std::to_integer<unsigned>(input[12 + i]) << (8 * i);
    auto j = nlohmann::json::parse(input.begin() + 24, input.begin() + 24 + n);
    change(j);
    const auto text = j.dump();
    auto out = input;
    out.resize(24);
    for (unsigned i = 0; i < 4; ++i)
        out[12 + i] = std::byte((text.size() >> (8 * i)) & 255);
    out.insert(out.end(), reinterpret_cast<const std::byte*>(text.data()),
               reinterpret_cast<const std::byte*>(text.data() + text.size()));
    out.insert(out.end(), input.begin() + 24 + n, input.end());
    return out;
}
} // namespace
int main(int argc, char** argv) {
    try {
        require(argc == 2, "Need texture scratch folder");
        for (unsigned i = 0; i <= unsigned(TextureFormat::BC7Srgb); ++i) {
            auto t = texture(TextureFormat(i));
            const auto encoded = encode_texture(t);
            require(encode_texture(decode_texture(encoded)) == encoded,
                    "Texture format roundtrip changed");
        }
        for (auto d : {TextureDimension::D2, TextureDimension::D2Array, TextureDimension::Cube,
                       TextureDimension::CubeArray, TextureDimension::D3}) {
            auto t = texture(TextureFormat::RGBA16Float, d);
            for (std::size_t i = 0; i < t.subresources.size(); ++i)
                t.subresources[i][0] = std::byte(i);
            const auto e = encode_texture(t);
            require(decode_texture(e).subresources == t.subresources,
                    "Layer/face/mip order changed");
            require(encode_texture(decode_texture(e)) == e, "Texture dimension roundtrip changed");
        }
        auto t = texture(TextureFormat::BC1);
        t.width = 7;
        t.height = 5;
        t.mips = 3;
        t.subresources.clear();
        require(texture_layout(t, 0).row_bytes == 16 && texture_layout(t, 0).bytes == 32 &&
                    texture_layout(t, 2).bytes == 8,
                "Block rounding or tail mips incorrect");
        for (unsigned mip = 0; mip < t.mips; ++mip)
            t.subresources.emplace_back(texture_layout(t, mip).bytes);
        validate_texture(t);
        const auto bytes = encode_texture(texture());
        for (std::size_t i = 0; i < bytes.size(); ++i)
            rejects([&] { decode_texture(std::span(bytes).first(i)); });
        auto changed = bytes;
        changed.push_back(std::byte{0});
        rejects([&] { decode_texture(changed); });
        for (unsigned position : {0u, 8u, 12u, 16u, 20u}) {
            changed = bytes;
            changed[position] = std::byte{255};
            rejects([&] { decode_texture(changed); });
        }
        for (const auto* key : {"width", "height", "depth", "layers", "mips"})
            for (auto value : {-1, 0, 2147483647}) {
                auto invalid = rewrite(bytes, [&](auto& j) { j[key] = value; });
                rejects([&] { decode_texture(invalid); });
            }
        rejects([&] { decode_texture(rewrite(bytes, [](auto& j) { j["width"] = 8.0; })); });
        rejects([&] { decode_texture(rewrite(bytes, [](auto& j) { j["format"] = 27; })); });
        rejects([&] {
            decode_texture(rewrite(bytes, [](auto& j) { j["sampler"]["border"] = {0, 0, 0}; }));
        });
        TextureLimits small;
        small.bytes = 15;
        rejects([&] { decode_texture(bytes, small); });
        small = {};
        small.subresources = 3;
        rejects([&] { decode_texture(bytes, small); });
        t = texture(TextureFormat::RGBA8Srgb);
        t.semantic = TextureSemantic::Normal;
        rejects([&] { validate_texture(t); });
        t = texture(TextureFormat::RG16Float);
        t.semantic = TextureSemantic::Normal;
        validate_texture(t);
        t = texture(TextureFormat::RGBA16Float);
        t.subresources[0][1] = std::byte{0x7c};
        rejects([&] { validate_texture(t); });
        t = texture(TextureFormat::R32Float);
        t.subresources[0][2] = std::byte{0x80};
        t.subresources[0][3] = std::byte{0x7f};
        rejects([&] { validate_texture(t); });
        t = texture();
        t.semantic = TextureSemantic::HdrColor;
        rejects([&] { validate_texture(t); });
        t = texture(TextureFormat::BC1, TextureDimension::D3);
        rejects([&] { validate_texture(t); });
        t = texture(TextureFormat::RGBA8, TextureDimension::Cube);
        t.height = 4;
        rejects([&] { validate_texture(t); });
        t = texture();
        t.subresources[0].pop_back();
        rejects([&] { validate_texture(t); });
        SamplerState sampler;
        sampler.min_lod = -1;
        sampler.border = {-1, 2, 0, 1};
        validate_sampler(sampler);
        sampler.anisotropy = 16;
        validate_sampler(sampler);
        sampler.min = TextureFilter::Nearest;
        rejects([&] { validate_sampler(sampler); });
        sampler = {};
        sampler.max_lod = -1;
        rejects([&] { validate_sampler(sampler); });
        sampler = {};
        sampler.border[0] = std::numeric_limits<float>::quiet_NaN();
        rejects([&] { validate_sampler(sampler); });
        const std::filesystem::path root = argv[1];
        std::filesystem::create_directories(root);
        const auto path = root / "texture.ftex";
        {
            std::ofstream out(path, std::ios::binary);
            out.write(reinterpret_cast<const char*>(bytes.data()), std::streamsize(bytes.size()));
        }
        ResourcePool<TextureAsset> pool;
        AssetRef<TextureAsset> id{AssetId::generate()};
        const auto digest = asset_detail::content_digest(bytes);
        auto request = pool.request(id, digest, 1, texture_resource_loader(path, digest), {}, 0,
                                    "linear.rgba8");
        require(pool.wait(request, 5s), "Real texture provider load failed");
        auto lease = pool.acquire(request);
        require(lease && lease->width == 8 && lease->mips == 4, "Texture lease lost dimensions");
        auto failure = pool.request(id, std::string(64, 'a'), 2,
                                    texture_resource_loader(path, std::string(64, 'a')), {}, 0,
                                    "linear.rgba8");
        require(!pool.wait(failure, 5s) &&
                    pool.current(id, "linear.rgba8").identity() == lease.identity(),
                "Bad texture replaced previous good");
        std::cout << "Texture dimensions, block layouts, admission, samplers and resource failure "
                     "tests passed\n";
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
