#include "editor/model_wire_view.hpp"
#include <iostream>
using namespace forge;
namespace {
void require(bool ok, const char* why) {
    if (!ok)
        throw std::runtime_error(why);
}
void near(double a, double b) {
    require(std::abs(a - b) < 1e-4, "Independent projected coordinate differs");
}
void verify(const EditableMeshSource& source, const ModelWireView& view, unsigned orientation) {
    require(view.projected.size() == source.document.at("vertices").size(),
            "Projected IDs missing");
    for (const auto& vertex : source.document.at("vertices")) {
        const auto id = vertex.at("id").get<unsigned>();
        const auto p = vertex.at("position").get<std::array<double, 3>>();
        const auto point = view.projected.at(id);
        // Independent axis-aligned camera oracles; the Cube spans one unit.
        const auto x = orientation ? -p[2] : p[0];
        const auto y = orientation == 2 ? p[0] : -p[1];
        const auto depth = orientation == 2 ? p[1] : orientation ? p[0] : p[2];
        near(point.x, 10 + 150 + x * 200);
        near(point.y, 20 + 120 + y * 200);
        near(view.depths.at(id), depth);
        require(view.positions.at(id) == p, "Derived cache changed original positions");
    }
    require(view.faces.size() == source.document.at("faces").size(),
            "Face draw order/count changed");
    for (std::size_t i = 0; i < view.faces.size(); ++i) {
        const auto& face = source.document.at("faces")[i];
        const auto& loop = view.faces[i];
        require(loop.id == face.at("id").get<unsigned>() &&
                    loop.loop.size() == face.at("corners").size(),
                "Face IDs/corner order changed");
        for (std::size_t j = 0; j < loop.loop.size(); ++j) {
            const auto p = view.projected.at(face.at("corners")[j].at("vertex").get<unsigned>());
            require(p.x == loop.loop[j].x && p.y == loop.loop[j].y,
                    "Cached draw/hit points differ");
        }
    }
}
} // namespace
int main() {
    try {
        auto source = EditableMeshSource::create_cube(AssetId::generate());
        const auto original = source.document;
        ModelWireViewCache cache;
        ModelWireViewKey key{source.asset(), 0, {}, 0, 0, {10, 20}, {300, 240}};
        require(cache.refresh(source, key), "Initial view did not rebuild");
        verify(source, cache.view(), 0);
        const auto generation = cache.generation();
        const auto* points = &cache.view().projected;
        for (unsigned i = 0; i < 30; ++i)
            require(!cache.refresh(source, key) && cache.generation() == generation &&
                        points == &cache.view().projected,
                    "Idle view did not reuse derived data");
        key.yaw = 90;
        require(cache.refresh(source, key), "Orbit did not invalidate");
        verify(source, cache.view(), 1);
        key.pitch = 90;
        require(cache.refresh(source, key), "Pitch did not invalidate");
        verify(source, cache.view(), 2);
        key.origin.x += 7;
        require(cache.refresh(source, key), "Origin did not invalidate");
        key.size.x += 20;
        require(cache.refresh(source, key), "Canvas did not invalidate");
        ++key.revision;
        require(cache.refresh(source, key), "Revision did not invalidate");
        key.preview = "valid candidate";
        require(cache.refresh(source, key), "Preview did not invalidate");
        source.inset_face(13, .1);
        ++key.revision;
        require(cache.refresh(source, key) && cache.view().projected.size() == 12 &&
                    cache.view().faces.size() == 10,
                "Preview topology retained stale derived IDs");
        source = {original};
        key.preview.clear();
        ++key.revision;
        require(cache.refresh(source, key) && cache.view().projected.size() == 8 &&
                    cache.view().faces.size() == 6,
                "Original view retained discarded candidate topology");
        auto other = EditableMeshSource::create_cube(AssetId::generate());
        key.asset = other.asset();
        require(cache.refresh(other, key), "Source identity did not invalidate");
        cache.clear();
        require(cache.view().projected.empty() && cache.view().faces.empty(),
                "Close retained view memory");
        require(cache.refresh(other, key), "Reopen did not rebuild");
        require(source.document == original, "Derived view altered authored source");
        std::cout << "Independent axis projection/depth/order, idle reuse and "
                     "source/revision/preview/view/close invalidation passed\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
