#pragma once
#include "../tile_asset_resources.hpp"
#include "editor_state.hpp"
#include <algorithm>
#include <functional>

namespace forge::ui {
struct PublishedTileMapView {
    std::shared_ptr<const TileMapData> cells;
    std::string revision, status;
    bool current = false;
};
using PublishedTileMapReader = std::function<PublishedTileMapView(AssetRef<TileMapAsset>)>;
inline bool tile_map_layer_field(const PublishedTileMapReader& read, const Json& components,
                                 Json& value) {
    try {
        if (!value.is_number_unsigned() &&
            (!value.is_number_integer() || value.get<std::int64_t>() < 0))
            throw std::invalid_argument("Solid layer must be an exact Map layer key");
        const auto selected = value.get<std::uint64_t>();
        AssetRef<TileMapAsset> asset;
        if (components.contains("forge.tile_map_renderer")) {
            const auto& map = components.at("forge.tile_map_renderer").at("map");
            if (!map.is_null())
                asset.id = map.get<AssetId>();
        }
        auto view = read ? read(asset) : PublishedTileMapView{};
        std::string label = selected ? "Missing layer (" + std::to_string(selected) + ")" : "None";
        if (view.cells)
            for (const auto& layer : view.cells->layers())
                if (layer.id == selected)
                    label = layer.name;
        bool changed = false;
        ImGui::TextUnformatted("Solid layer");
        ImGui::SetNextItemWidth(-1);
        if (ImGui::BeginCombo("##solid-layer", label.c_str())) {
            if (ImGui::Selectable("None", selected == 0)) {
                value = std::uint64_t{0};
                changed = true;
            }
            FORGE_UI_PROBE("tile_map_collider:layer:none");
            ImGui::BeginDisabled(!view.current);
            if (view.cells)
                for (const auto& layer : view.cells->layers()) {
                    const auto key = std::to_string(layer.id);
                    ImGui::PushID(key.c_str());
                    const auto name = layer.name + (layer.visible ? "" : " (hidden)");
                    if (ImGui::Selectable(name.c_str(), layer.id == selected)) {
                        value = layer.id;
                        changed = true;
                    }
                    FORGE_UI_PROBE("tile_map_collider:layer:" + key);
                    if (layer.id == selected)
                        ImGui::SetItemDefaultFocus();
                    ImGui::PopID();
                }
            ImGui::EndDisabled();
            ImGui::EndCombo();
        }
        FORGE_UI_PROBE("tile_map_collider:layer");
        const auto note = !asset.id             ? "Assign a Tile Map Renderer to choose its layers."
                          : view.current        ? "Published Map layers"
                          : view.status.empty() ? "Preparing published Map layers..."
                                                : view.status;
        ImGui::TextWrapped("%s", note.c_str());
        help("Occupied cells in this layer are solid. Hidden layers can provide collision; "
             "decorative layers and image flips do not change it. Use a Static Physics Body 2D.");
        return changed;
    } catch (const std::exception& error) {
        field_error(error.what());
        return false;
    }
}
} // namespace forge::ui
