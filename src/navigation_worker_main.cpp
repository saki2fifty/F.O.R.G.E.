#include "asset_bytes.hpp"
#include "json_document.hpp"
#include "navigation_asset.hpp"
#include "navigation_geometry.hpp"
#include <fstream>
#include <iostream>
#include <numeric>
// Dedicated bounded worker. The editor supplies an isolated staging working directory.
int main(int argc, char** argv) {
    try {
        if (argc != 2 || std::string(argv[1]) != "--build-navigation")
            return 2;
        using namespace forge;
        using namespace navigation_detail;
        auto bytes = asset_detail::read_bytes("source.json", 4 * 1024 * 1024);
        auto input = detail::parse_json_document(
            std::string_view(reinterpret_cast<const char*>(bytes.data()), bytes.size()),
            [](int depth, nlohmann::json::parse_event_t, nlohmann::json&) {
                if (depth > 16)
                    throw std::runtime_error("Navigation input nesting limit");
                return true;
            });
        const auto& v = input.at("vertices");
        if (!v.is_array() || v.empty() || v.size() % 9 || v.size() > 16384 * 9)
            throw std::runtime_error("Navigation geometry count limit");
        Geometry g;
        g.vertices = v.get<std::vector<float>>();
        g.indices.resize(g.vertices.size() / 3);
        std::iota(g.indices.begin(), g.indices.end(), 0);
        auto meta = input.at("metadata");
        const auto settings = provenance_settings(meta);
        if (meta.contains("links"))
            g.connections = read_navigation_links(meta.at("links"), settings.plane);
        if (settings.plane == NavigationPlane::PlanarXY) {
            const auto& masks = input.at("obstacles");
            if (!masks.is_array() || masks.size() > 512)
                throw std::runtime_error("Navigation obstacle count limit");
            for (const auto& mask : masks) {
                if (!mask.is_array() || mask.size() < 9 || mask.size() % 3 != 0 ||
                    mask.size() / 3 > navigation_region_vertex_limit)
                    throw std::runtime_error("Navigation obstacle requires 3..64 vertices");
                g.obstacles.push_back(mask.get<std::vector<float>>());
            }
        } else if (input.contains("obstacles")) {
            throw std::runtime_error("Spatial navigation does not accept planar obstacle masks");
        }
        auto tile = build_tile(g, settings);
        auto data = envelope(meta, tile);
        (void)admit(data);
        std::ofstream out("navigation.fnav", std::ios::binary | std::ios::trunc);
        if (!out.write(reinterpret_cast<const char*>(data.data()), std::streamsize(data.size())) ||
            !out.flush())
            throw std::runtime_error("Cannot write navigation candidate");
        return 0;
    } catch (const std::exception& e) {
        std::string error = e.what();
        error.resize(std::min<std::size_t>(error.size(), 8192));
        std::ofstream("error.txt") << error;
        std::cerr << error << '\n';
        return 1;
    }
}
