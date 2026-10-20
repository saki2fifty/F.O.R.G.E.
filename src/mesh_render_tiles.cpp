#include "mesh_render_host.hpp"
#include "sprite_render_values.hpp"
#include "tile_map_data.hpp"
#include <forge/engine_assets.hpp>
#include <set>

namespace forge {
namespace {
void require(bool value, const char* why) {
    if (!value)
        throw std::runtime_error(why);
}
std::array<float, 4> tint(const TileMapRenderer& values) {
    return {values.tint_r, values.tint_g, values.tint_b, values.tint_a};
}
} // namespace
void MeshResourceHost::preview_tile_set(AssetRef<TileSetAsset> ref,
                                        std::shared_ptr<const TileSetData> data) {
    check_thread();
    require(isolated_source_preview_ && ref.id && data && data->asset() == ref.id &&
                epoch_ != UINT64_MAX,
            "Unsaved Tile Set requires an isolated host and matching source identity");
    const auto found = tile_set_previews_.find(ref.id);
    if (found != tile_set_previews_.end() && found->second == data)
        return;
    require(found != tile_set_previews_.end() || tile_set_previews_.empty(),
            "Isolated Tile Set preview accepts one source at a time");
    data->validate();
    tile_set_previews_[ref.id] = std::move(data);
    ++epoch_;
}
void MeshResourceHost::preview_tile_map(AssetRef<TileMapAsset> ref,
                                        std::shared_ptr<const TileMapData> data) {
    check_thread();
    require(isolated_source_preview_ && ref.id && data && data->asset() == ref.id &&
                epoch_ != UINT64_MAX,
            "Unsaved Tile Map requires an isolated host and matching source identity");
    const auto found = tile_map_previews_.find(ref.id);
    if (found != tile_map_previews_.end() && found->second == data)
        return;
    require(found != tile_map_previews_.end() || tile_map_previews_.empty(),
            "Isolated Tile Map preview accepts one source at a time");
    data->validate();
    tile_map_previews_[ref.id] = std::move(data);
    ++epoch_;
}
RenderTileMap MeshSceneRenderer::resolve_tile_assets(TileDrawSource& source,
                                                     const RenderTileMap& input) {
    // A missing runtime snapshot is pending/failed runtime data, never permission
    // to substitute authored cells for an independently edited running instance.
    if (input.runtime_instance || input.cells || input.definitions) {
        source.load = {};
        return input;
    }
    auto result = input;
    auto& load = source.load;
    if (load.asset != input.renderer.map || load.epoch != host_->epoch_) {
        load = {};
        load.asset = input.renderer.map;
        load.epoch = host_->epoch_;
        try {
            const auto preview = host_->tile_map_previews_.find(load.asset.id);
            if (preview != host_->tile_map_previews_.end())
                load.cells = preview->second;
            else
                load.map_ticket = request_tile_map(host_->tile_maps_, host_->project_,
                                                   host_->catalog_, load.asset);
        } catch (const std::exception& error) {
            load.error = error.what();
        }
    }
    try {
        require(load.error.empty(), load.error.c_str());
        if (!load.cells) {
            load.map_lease = host_->tile_maps_.acquire(load.map_ticket);
            if (!load.map_lease) {
                const auto state = load.map_ticket.inspect();
                if (resource_detail::terminal(state.state))
                    throw std::runtime_error("Tile Map load failed: " + state.diagnostic);
                result.pending = true;
                return result;
            }
            // Keep the pool lease charged while an older complete draw uses its
            // shared chunks. The copied pure root also survives scope retirement.
            auto retained = std::make_shared<std::pair<ResourceLease<TileMapAsset>, TileMapData>>(
                load.map_lease, load.map_lease.get());
            load.cells = {retained, &retained->second};
        }
        if (!load.definitions) {
            if (!load.set_ticket) {
                const auto preview = host_->tile_set_previews_.find(load.cells->tile_set());
                if (preview != host_->tile_set_previews_.end())
                    load.definitions = preview->second;
                else
                    load.set_ticket = request_tile_set(host_->tile_sets_, host_->project_,
                                                       host_->catalog_, {load.cells->tile_set()});
            }
            if (!load.definitions) {
                load.set_lease = host_->tile_sets_.acquire(load.set_ticket);
                if (!load.set_lease) {
                    const auto state = load.set_ticket.inspect();
                    if (resource_detail::terminal(state.state))
                        throw std::runtime_error("Tile Set load failed: " + state.diagnostic);
                    result.pending = true;
                    return result;
                }
                auto retained =
                    std::make_shared<std::pair<ResourceLease<TileSetAsset>, TileSetData>>(
                        load.set_lease, load.set_lease.get());
                load.definitions = {retained, &retained->second};
            }
        }
        result.cells = load.cells;
        result.definitions = load.definitions;
    } catch (const std::exception& error) {
        load.error = error.what();
        result.pending = false;
        result.diagnostic = load.error;
    }
    return result;
}
std::uint64_t MeshSceneRenderer::tile_material_bytes() const {
    std::uint64_t result = 0;
    for (const auto& [id, source] : tile_sources_) {
        (void)id;
        // Include conservative ordered-map node/control storage, not allocator RSS.
        const auto count = source.values.size() *
                           (sizeof(decltype(source.values)::value_type) + 4 * sizeof(void*));
        if (count > material_budget_ - result)
            throw std::runtime_error("Tile image values exceed the scene payload budget");
        result += count;
    }
    return result;
}
std::vector<MeshSceneRenderer::TileImageInput>
MeshSceneRenderer::prepare_tile_sources(const RenderScene& scene) {
    require(scene.tile_maps.size() <= 256, "Scene exceeds the 256 Tile Map renderer profile");
    std::set<EntityId> used;
    for (const auto& map : scene.tile_maps)
        require(used.insert(map.entity).second, "Presentation repeats a Tile Map actor");
    std::erase_if(tile_sources_, [&](const auto& value) { return !used.contains(value.first); });
    if (tile_sources_.empty())
        std::vector<AffineTransform>{}.swap(tile_worlds_);
    std::vector<TileImageInput> inputs;
    for (const auto& input : scene.tile_maps) {
        auto& source = tile_sources_[input.entity];
        // A runtime scope change retires its complete draw as well as its
        // requested snapshot. An empty copied view cannot retain old-world tiles.
        if (input.runtime_instance &&
            (!input.cells || !input.definitions || !source.requested.runtime_instance ||
             source.requested.generation != input.generation ||
             source.requested.renderer.map != input.renderer.map))
            source = {};
        if (!input.renderer.map.id || !input.renderer.enabled) {
            source = {};
            source.requested = input;
            continue;
        }
        const auto map = resolve_tile_assets(source, input);
        const bool changed =
            source.requested.cells != map.cells || source.requested.definitions != map.definitions;
        source.requested = map;
        source.pending = map.pending;
        source.rejected = false;
        try {
            detail::validate_tile_map_renderer(map.renderer);
            if (!map.diagnostic.empty() && !map.pending)
                report(map.entity, map.diagnostic, DrawScope::TileMap);
            if (!map.cells || !map.definitions) {
                if (!map.pending && map.diagnostic.empty())
                    report(map.entity, "Tile Map data is unavailable", DrawScope::TileMap);
                continue;
            }
            require(map.cells->asset() == map.renderer.map.id,
                    "Tile Map draw cells have a different root identity");
            require(map.cells->tile_set() == map.definitions->asset(),
                    "Tile Map cells and definitions have different bindings");
            if (changed) {
                std::set<std::uint64_t> images;
                std::map<std::uint64_t, RenderBounds> bounds;
                std::size_t visible_cells = 0;
                const auto size = map.cells->cell_size();
                map.cells->for_each_chunk([&](auto chunk, auto cells, auto occupied) {
                    const auto& layers = map.cells->layers();
                    const auto layer =
                        std::find_if(layers.begin(), layers.end(),
                                     [&](const auto& value) { return value.id == chunk.layer; });
                    if (layer != layers.end() && layer->visible)
                        visible_cells += occupied;
                    for (std::uint32_t slot = 0; slot < tile_chunk_cells; ++slot) {
                        if (!cells[slot].tile)
                            continue;
                        images.insert(map.definitions->tile(cells[slot].tile).image);
                        const auto at = tile_cell_address({chunk.address, slot});
                        const RenderBounds cell{
                            {double(at.x) * size[0], double(at.y) * size[1], 0},
                            {(double(at.x) + 1) * size[0], (double(at.y) + 1) * size[1], 0}};
                        auto [row, added] = bounds.try_emplace(chunk.layer, cell);
                        if (!added)
                            for (unsigned axis = 0; axis < 3; ++axis) {
                                row->second.minimum[axis] =
                                    std::min(row->second.minimum[axis], cell.minimum[axis]);
                                row->second.maximum[axis] =
                                    std::max(row->second.maximum[axis], cell.maximum[axis]);
                            }
                    }
                });
                source.used_images.assign(images.begin(), images.end());
                source.requested_bounds = std::move(bounds);
                source.requested_visible_cells = visible_cells;
            }
            for (const auto image : source.used_images) {
                const auto& definition = map.definitions->image(image);
                TileImageInput input;
                input.image = image;
                input.mesh.entity = map.entity;
                input.mesh.world = map.world;
                input.mesh.selectable = false; // Cells use the map's grid selection.
                input.mesh.renderer.mesh = engine_primitive(8);
                input.mesh.renderer.materials.push_back({"surface", definition.material});
                input.mesh.renderer.cast_shadows = input.mesh.renderer.receive_shadows = false;
                input.mesh.renderer.layers = map.renderer.layers;
                input.mesh.renderer.visible = map.renderer.visible;
                // This proxy admits one shared Quad/image bundle. Per-cell frame,
                // dimensions, flags and tint never come from its placeholder values.
                input.sprite.material = definition.material;
                inputs.push_back(std::move(input));
            }
        } catch (const std::exception& error) {
            source.pending = false;
            source.rejected = true;
            report(map.entity, error.what(), DrawScope::TileMap);
        }
    }
    return inputs;
}
void MeshSceneRenderer::adopt_tile_images() {
    for (auto& [entity, source] : tile_sources_) {
        const auto& request = source.requested;
        if (source.rejected || !request.cells || !request.definitions || !request.renderer.map.id ||
            !request.renderer.enabled)
            continue;
        try {
            // Cell edits preserve the definition rows and usually every image
            // selection. Reuse those complete groups without per-frame map nodes.
            const bool same_groups =
                source.definitions &&
                source.definitions->tiles().data() == request.definitions->tiles().data() &&
                source.color == tint(request.renderer) &&
                source.images.size() == source.used_images.size() &&
                std::all_of(source.used_images.begin(), source.used_images.end(), [&](auto image) {
                    const auto current = entries_.find({entity, DrawScope::TileMap, image});
                    const auto previous = source.images.find(image);
                    return current != entries_.end() && previous != source.images.end() &&
                           current->second.resource_current && current->second.sprite &&
                           current->second.sprite->ready &&
                           current->second.ready == previous->second.bundle &&
                           current->second.sprite->texture_width == previous->second.width &&
                           current->second.sprite->texture_height == previous->second.height;
                });
            if (same_groups) {
                if (source.cells != request.cells)
                    source.bounds = source.requested_bounds;
                source.cells = request.cells;
                source.definitions = request.definitions;
                source.visible_cells = source.requested_visible_cells;
                continue;
            }
            std::map<std::uint64_t, TileImageDraw> images;
            bool complete = true;
            for (const auto image : source.used_images) {
                const auto found = entries_.find({entity, DrawScope::TileMap, image});
                if (found == entries_.end() || !found->second.ready ||
                    !found->second.resource_current || !found->second.sprite ||
                    !found->second.sprite->ready) {
                    source.pending |= found != entries_.end() && found->second.candidate &&
                                      found->second.candidate->state() == ResourceState::Loading;
                    complete = false;
                    break;
                }
                const auto input =
                    std::find_if(tile_images_.begin(), tile_images_.end(), [&](const auto& item) {
                        return item.mesh.entity == entity && item.image == image;
                    });
                require(input != tile_images_.end(), "Tile image has no requested proxy");
                const auto& entry = found->second;
                require(entry.ready->profile() == MeshDrawProfile::Sprite &&
                            entry.ready->parts(0).size() == 1,
                        "Tile image requires one complete Sprite Quad part");
                images.emplace(image,
                               TileImageDraw{entry.ready, input->mesh, entry.sprite->texture_width,
                                             entry.sprite->texture_height});
            }
            if (!complete)
                continue;
            bool invalidate =
                !source.definitions ||
                source.definitions->tiles().data() != request.definitions->tiles().data() ||
                source.color != tint(request.renderer) || source.images.size() != images.size();
            if (!invalidate)
                for (const auto& [key, image] : images) {
                    const auto previous = source.images.find(key);
                    invalidate |= previous == source.images.end() ||
                                  previous->second.bundle != image.bundle ||
                                  previous->second.width != image.width ||
                                  previous->second.height != image.height;
                }
            if (invalidate) {
                // Admit every used image's frame layout before replacing any of a
                // map's complete retained draw. No mixed old/new image selection.
                for (const auto& tile : request.definitions->tiles()) {
                    const auto image = images.find(tile.image);
                    if (image != images.end())
                        (void)tile_draw_values(*request.definitions, tile.id, 0,
                                               image->second.width, image->second.height,
                                               tint(request.renderer));
                }
                source.values.clear();
            }
            source.cells = request.cells;
            source.definitions = request.definitions;
            source.images = std::move(images);
            source.bounds = source.requested_bounds;
            source.visible_cells = source.requested_visible_cells;
            source.color = tint(request.renderer);
        } catch (const std::exception& error) {
            source.pending = false;
            report(entity, error.what(), DrawScope::TileMap);
        }
    }
}
} // namespace forge
