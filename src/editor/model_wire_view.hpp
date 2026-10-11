#pragma once
#include <algorithm>
#include <cmath>
#include <forge/editable_mesh.hpp>
#include <imgui.h>
#include <map>
#include <optional>
#include <string>
#include <vector>
namespace forge {
// Derived editor geometry only. The caller supplies a validated source and its
// current revision/valid-preview identity; no authoring or runtime data is owned here.
struct ModelWireViewKey {
    AssetId asset;
    std::uint64_t revision = 0;
    std::string preview;
    float yaw = 0, pitch = 0;
    ImVec2 origin{}, size{};
    bool operator==(const ModelWireViewKey& b) const {
        return asset == b.asset && revision == b.revision && preview == b.preview && yaw == b.yaw &&
               pitch == b.pitch && origin.x == b.origin.x && origin.y == b.origin.y &&
               size.x == b.size.x && size.y == b.size.y;
    }
};
struct ModelWireView {
    struct Face {
        std::uint32_t id = 0;
        std::vector<ImVec2> loop;
    };
    std::map<std::uint32_t, ImVec2> projected;
    std::map<std::uint32_t, double> depths;
    std::map<std::uint32_t, std::array<double, 3>> positions;
    std::vector<Face> faces;
    float min_x = 1e9f, min_y = 1e9f, max_x = -1e9f, max_y = -1e9f, fit = 0;
};
class ModelWireViewCache {
  public:
    const ModelWireView& view() const { return view_; }
    std::uint64_t generation() const { return generation_; }
    void clear() {
        key_.reset();
        view_ = {};
    }
    bool refresh(const EditableMeshSource& source, ModelWireViewKey key) {
        if (key_ && *key_ == key)
            return false;
        ModelWireView next;
        const double yaw = key.yaw * 3.14159265358979323846 / 180.0;
        const double pitch = key.pitch * 3.14159265358979323846 / 180.0;
        for (const auto& vertex : source.document.at("vertices")) {
            const auto& p = vertex.at("position");
            const double x = p[0].get<double>(), y = p[1].get<double>(), z = p[2].get<double>();
            const auto horizontal = std::cos(yaw) * x - std::sin(yaw) * z;
            const auto depth = std::sin(yaw) * x + std::cos(yaw) * z;
            const auto vertical = std::cos(pitch) * y - std::sin(pitch) * depth;
            const auto id = vertex.at("id").get<std::uint32_t>();
            next.projected[id] = {static_cast<float>(horizontal), static_cast<float>(-vertical)};
            next.depths[id] = std::cos(pitch) * depth + std::sin(pitch) * y;
            next.positions[id] = {x, y, z};
            next.min_x = std::min(next.min_x, next.projected[id].x);
            next.min_y = std::min(next.min_y, next.projected[id].y);
            next.max_x = std::max(next.max_x, next.projected[id].x);
            next.max_y = std::max(next.max_y, next.projected[id].y);
        }
        next.fit = std::min((key.size.x - 40) / std::max(.01f, next.max_x - next.min_x),
                            (key.size.y - 40) / std::max(.01f, next.max_y - next.min_y));
        for (auto& [id, point] : next.projected) {
            (void)id;
            point.x = key.origin.x + key.size.x * .5f +
                      (point.x - (next.min_x + next.max_x) * .5f) * next.fit;
            point.y = key.origin.y + key.size.y * .5f +
                      (point.y - (next.min_y + next.max_y) * .5f) * next.fit;
        }
        next.faces.reserve(source.document.at("faces").size());
        for (const auto& polygon : source.document.at("faces")) {
            ModelWireView::Face face;
            face.id = polygon.at("id").get<std::uint32_t>();
            face.loop.reserve(polygon.at("corners").size());
            for (const auto& corner : polygon.at("corners"))
                face.loop.push_back(next.projected.at(corner.at("vertex").get<std::uint32_t>()));
            next.faces.push_back(std::move(face));
        }
        view_ = std::move(next);
        key_ = std::move(key);
        ++generation_;
        return true;
    }

  private:
    ModelWireView view_;
    std::optional<ModelWireViewKey> key_;
    std::uint64_t generation_ = 0;
};
} // namespace forge
