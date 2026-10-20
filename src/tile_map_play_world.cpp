#include "tile_map_play_world.hpp"
#include "runtime_tile_map.hpp"
#include <exception>
#include <forge/render_components.hpp>
#include <stdexcept>

namespace forge {
std::vector<TileMapPlayView> tile_map_play_views(WorldContext& context) {
    if (!context.on_owner_thread() || context.role() != WorldRole::Runtime)
        throw std::runtime_error("Tile Play observations require their runtime world owner");
    std::vector<TileMapPlayView> result;
    result.reserve(256);
    std::exception_ptr failure;
    context.world().each([&](flecs::entity entity, const TileMapRenderer& renderer) {
        if (failure || !renderer.enabled || !renderer.map.id)
            return;
        try {
            const auto reference = context.reference(entity.id());
            if (!reference)
                return;
            if (result.size() == 256)
                throw std::runtime_error("Tile Play observations exceed 256 map actors");
            const auto status = detail::inspect_runtime_tile_map(context, *reference);
            TileMapPlayView view;
            view.entity = reference->entity;
            view.requested = status.requested;
            view.pending = status.pending;
            view.diagnostic = status.diagnostic;
            if (status.ready && status.adopted == status.requested) {
                const auto snapshot = detail::runtime_tile_map_snapshot(context, *reference);
                view.generation = snapshot->generation;
                view.revision = snapshot->revision;
                view.edited = snapshot->edited;
                view.map_revision = snapshot->asset.identity().revision;
                view.set_revision = snapshot->definitions.identity().revision;
                view.cells = {snapshot, &snapshot->cells};
                view.definitions = {snapshot, &snapshot->definition_values};
            }
            result.push_back(std::move(view));
        } catch (...) {
            failure = std::current_exception();
        }
    });
    if (failure)
        std::rethrow_exception(failure);
    return result;
}
} // namespace forge
