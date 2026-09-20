#include <algorithm>
#include <bit>
#include <forge/mesh_asset.hpp>
#include <iostream>
#include <limits>
using namespace forge;
namespace {
void require(bool value, const char* why) {
    if (!value)
        throw std::runtime_error(why);
}
template <class F> void rejects(F fn) {
    try {
        fn();
    } catch (const std::exception&) {
        return;
    }
    throw std::runtime_error("Malformed mesh accepted");
}
MeshData triangle() {
    MeshPart part;
    part.vertices = 3;
    part.streams = {{"POSITION", 3, std::vector<float>{-1, 0, 0, 1, 0, 0, 0, 1, 0}},
                    {"NORMAL", 3, std::vector<float>{0, 0, 1, 0, 0, 1, 0, 0, 1}},
                    {"TEXCOORD_0", 2, std::vector<float>{0, 0, 1, 0, .5, 1}},
                    {"TANGENT", 4, std::vector<float>{1, 0, 0, 1, 1, 0, 0, 1, 1, 0, 0, 1}},
                    {"_EXACT", 1, std::vector<std::uint32_t>{16777217, 2, 3}}};
    part.indices = {0, 1, 2};
    part.bounds = mesh_bounds(part);
    return {1, {{1, {part}}}, {}, {}};
}
void write(std::vector<std::byte>& bytes, std::size_t at, std::uint32_t value) {
    for (unsigned i = 0; i < 4; ++i)
        bytes.at(at + i) = std::byte((value >> (i * 8)) & 255);
}
std::uint32_t read(const std::vector<std::byte>& bytes, std::size_t at) {
    std::uint32_t value = 0;
    for (unsigned i = 0; i < 4; ++i)
        value |= std::to_integer<std::uint32_t>(bytes.at(at + i)) << (i * 8);
    return value;
}
template <class F> std::vector<std::byte> edit(const std::vector<std::byte>& bytes, F fn) {
    const auto n = read(bytes, 12);
    auto j = nlohmann::json::parse(bytes.begin() + 24, bytes.begin() + 24 + n);
    fn(j);
    const auto text = j.dump();
    std::vector<std::byte> result(bytes.begin(), bytes.begin() + 24);
    write(result, 12, static_cast<std::uint32_t>(text.size()));
    for (auto ch : text)
        result.push_back(std::byte(ch));
    result.insert(result.end(), bytes.begin() + 24 + n, bytes.end());
    return result;
}
} // namespace
int main() {
    try {
        auto mesh = triangle();
        const auto bytes = encode_mesh(mesh);
        auto decoded = decode_mesh(bytes);
        require(encode_mesh(decoded) == bytes, "Mesh roundtrip changed bytes");
        require(std::get<std::vector<std::uint32_t>>(
                    decoded.lods[0].parts[0].find("_EXACT")->values)[0] == 16777217,
                "Integer vertex data lost precision");
        std::reverse(mesh.lods[0].parts[0].streams.begin(), mesh.lods[0].parts[0].streams.end());
        require(encode_mesh(mesh) == bytes, "Stream input ordering changed canonical cook");
        auto bad = [&](auto fn) {
            auto copy = triangle();
            fn(copy);
            rejects([&] { encode_mesh(copy); });
        };
        bad([](auto& m) { m.lods[0].parts[0].indices[2] = 3; });
        bad([](auto& m) { m.lods[0].parts[0].indices.pop_back(); });
        bad([](auto& m) { m.lods[0].parts[0].bounds.minimum[0] = -2; });
        bad([](auto& m) { m.lods[0].parts[0].streams[0].components = 2; });
        bad([](auto& m) {
            std::get<std::vector<float>>(m.lods[0].parts[0].streams[0].values)[0] =
                std::numeric_limits<float>::quiet_NaN();
        });
        bad([](auto& m) { m.lods[0].parts[0].streams.push_back(m.lods[0].parts[0].streams[0]); });
        bad([](auto& m) { m.lods[0].parts[0].material_slot = 1; });
        bad([](auto& m) { m.lods.push_back(m.lods[0]); });
        for (std::size_t n = 0; n < bytes.size(); ++n)
            rejects([&] { decode_mesh(std::span(bytes).first(n)); });
        auto corrupt = bytes;
        corrupt.push_back(std::byte{0});
        rejects([&] { decode_mesh(corrupt); });
        corrupt = bytes;
        write(corrupt, 8, 2);
        rejects([&] { decode_mesh(corrupt); });
        corrupt = bytes;
        write(corrupt, 12, UINT32_MAX);
        rejects([&] { decode_mesh(corrupt); });
        corrupt = bytes;
        write(corrupt, 20, 1);
        rejects([&] { decode_mesh(corrupt); });
        for (unsigned which = 0; which < 7; ++which) {
            auto changed = edit(bytes, [&](auto& j) {
                auto& p = j["lods"][0]["parts"][0];
                if (which == 0)
                    p["vertices"] = -1;
                if (which == 1)
                    p["streams"][0]["count"] = UINT64_MAX;
                if (which == 2)
                    p["streams"][0]["offset"] = 4;
                if (which == 3)
                    p["streams"][0]["type"] = "u8";
                if (which == 4)
                    p["indices"]["offset"] = 0;
                if (which == 5)
                    p["maximum"] = nlohmann::json::array({0, 0});
                if (which == 6)
                    j["lods"][0]["coverage"] = -1;
            });
            rejects([&] { decode_mesh(changed); });
        }
        corrupt = bytes;
        write(corrupt, 24 + read(bytes, 12), 0x7fc00000);
        rejects([&] { decode_mesh(corrupt); });
        MeshLimits limits;
        limits.vertices = 2;
        rejects([&] { decode_mesh(bytes, limits); });
        limits = {};
        limits.bytes = 8;
        rejects([&] { decode_mesh(bytes, limits); });
        mesh = triangle();
        auto& part = mesh.lods[0].parts[0];
        part.morph_targets = {{{"POSITION", 3, std::vector<float>{0, 0, 1, 0, 0, 2, 0, 0, 3}}}};
        mesh.morph_names = {"Bend"};
        mesh.morph_defaults = {.25};
        mesh.lods.push_back(mesh.lods[0]);
        mesh.lods.back().screen_coverage = 0;
        const auto animated = encode_mesh(mesh);
        require(encode_mesh(decode_mesh(animated)) == animated, "LOD/morph roundtrip changed");
        auto orphan_delta = mesh;
        std::erase_if(orphan_delta.lods[0].parts[0].streams,
                      [](const auto& stream) { return stream.semantic == "NORMAL"; });
        orphan_delta.lods[0].parts[0].morph_targets[0].push_back(
            {"NORMAL", 3, std::vector<float>(9, 0.f)});
        rejects([&] { encode_mesh(orphan_delta); });
        orphan_delta.lods[0].parts[0].streams.push_back(
            {"NORMAL", 3, std::vector<float>{0, 0, 1, 0, 0, 1, 0, 0, 1}});
        require(!encode_mesh(orphan_delta).empty(), "Supported normal morph was rejected");
        for (auto topology : {MeshTopology::Points, MeshTopology::Lines}) {
            auto simple = triangle();
            auto& p = simple.lods[0].parts[0];
            p.topology = topology;
            p.indices = topology == MeshTopology::Lines ? std::vector<std::uint32_t>{0, 1}
                                                        : std::vector<std::uint32_t>{0, 1, 2};
            require(decode_mesh(encode_mesh(simple)).lods[0].parts[0].topology == topology,
                    "Topology changed");
        }
        std::cout << "Mesh cook, admission, canonical streams, exact integers, LODs, morphs and "
                     "corrupt/truncated rejection passed\n";
    } catch (const std::exception& e) {
        std::cerr << e.what() << "\n";
        return 1;
    }
}
