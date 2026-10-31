#include "mesh_render_host.hpp"
#include "sprite_draw_selection.hpp"
#include <tuple>

namespace forge {
namespace {
bool same_layout(const SpriteRenderer& a, const SpriteRenderer& b) {
    const auto fields = [](const SpriteRenderer& value) {
        return std::tie(value.columns, value.rows, value.margin_x, value.margin_y, value.spacing_x,
                        value.spacing_y, value.region_enabled, value.region_x, value.region_y,
                        value.region_width, value.region_height, value.frame_regions);
    };
    return fields(a) == fields(b);
}
SpriteFrameMipPlan tile_mip_plan(const TileSetData& definitions, std::uint64_t image,
                                 const TextureData& texture) {
    const auto& source = definitions.image(image);
    std::set<std::uint32_t> frames;
    for (const auto& tile : definitions.tiles())
        if (tile.image == image) {
            if (tile.animation && tile.animation->enabled) {
                for (const auto& frame : tile.animation->frames)
                    frames.insert(frame.frame);
            } else {
                frames.insert(tile.frame);
            }
        }
    if (frames.empty() || frames.size() > sprite_frame_region_limit)
        throw std::invalid_argument("Tile mip filtering needs 1 to 512 defined frames per image");
    SpriteRenderer layout;
    layout.frame_regions.reserve(frames.size());
    for (const auto frame : frames) {
        const auto region = sprite_grid_region(texture.width, texture.height, source.grid, frame);
        layout.frame_regions.push_back({region.x, region.y, region.width, region.height});
    }
    return sprite_frame_mip_plan(layout, texture, sprite_frame_mip_payload_limit);
}
} // namespace
std::uint64_t MeshSceneRenderer::SpriteMipBank::metadata_bytes() const {
    return sizeof(SpriteMipBank) + layout.frame_regions.capacity() * sizeof(SpriteFrameRegion) +
           (plan.frames.capacity() + plan.unique.capacity()) * sizeof(SpriteFrameMipKey) +
           frames.size() * (sizeof(decltype(frames)::value_type) + 4 * sizeof(void*)) +
           source.type.capacity() + source.revision.capacity() + source.variant.capacity();
}
std::uint64_t MeshSceneRenderer::SpriteValues::metadata_bytes() const {
    auto bytes = std::uint64_t(sizeof(SpriteValues)) +
                 (requested.frame_regions.capacity() + admitted.frame_regions.capacity()) *
                     sizeof(SpriteFrameRegion);
    if (mips)
        bytes += mips->metadata_bytes();
    if (mip_candidate && mip_candidate != mips)
        bytes += mip_candidate->metadata_bytes();
    return bytes;
}
std::shared_ptr<MeshSceneRenderer::SpriteMipBank>
MeshSceneRenderer::prepare_sprite_mips(Entry& entry,
                                       const asset_detail::PreparedModelDraw& prepared,
                                       EntityId entity, DrawScope scope, std::uint64_t image) {
    auto& values = *entry.sprite;
    values.mip_pending = false;
    if (!values.requested.mip_filter) {
        values.mip_candidate.reset();
        return {};
    }
    const auto& source = asset_detail::sprite_source_texture_lease(prepared);
    const auto definitions = scope == DrawScope::TileMap
                                 ? tile_sources_.at(entity).requested.definitions
                                 : std::shared_ptr<const TileSetData>{};
    const auto matches = [&](const auto& bank) {
        return bank && bank->source == source.identity() && bank->definitions == definitions &&
               same_layout(bank->layout, values.requested);
    };
    if (!matches(values.mip_candidate)) {
        if (matches(values.mips))
            values.mip_candidate = values.mips;
        else {
            auto next = std::make_shared<SpriteMipBank>();
            next->plan = definitions ? tile_mip_plan(*definitions, image, source.get())
                                     : sprite_frame_mip_plan(values.requested, source.get(),
                                                             sprite_frame_mip_payload_limit);
            next->source = source.identity();
            next->layout = values.requested;
            next->definitions = definitions;
            for (const auto& key : next->plan.unique)
                next->frames.emplace(key, SpriteMipBank::Frame{});
            auto retained = material_payload_bytes();
            if (values.mip_candidate && values.mip_candidate != values.mips)
                retained -= values.mip_candidate->metadata_bytes();
            if (retained > material_budget_ || next->metadata_bytes() > material_budget_ - retained)
                throw std::runtime_error("Sprite mip metadata exceeds the scene payload budget");
            values.mip_candidate = std::move(next);
        }
    }
    auto& bank = *values.mip_candidate;
    if (bank.complete)
        return values.mip_candidate;
    bool complete = true;
    for (auto& [key, frame] : bank.frames) {
        if (frame.pixels)
            continue;
        const auto identity = MeshResourceHost::sprite_mip_identity(bank.source, key);
        auto current = host_->textures_.current({bank.source.asset}, identity.variant);
        if (current && current.identity().revision == identity.revision) {
            frame.pixels = std::move(current);
            continue;
        }
        if (!frame.ticket)
            frame.ticket = host_->request_sprite_mip(source, key, sprite_frame_mip_payload_limit);
        if (frame.ticket) {
            const auto info = frame.ticket.inspect();
            if (info.state == ResourceState::Ready) {
                frame.pixels = host_->textures_.acquire(frame.ticket);
                if (frame.pixels)
                    continue;
            } else if (resource_detail::terminal(info.state))
                throw std::runtime_error("Sprite frame mip preparation failed: " + info.diagnostic);
        }
        complete = false;
    }
    if (!complete) {
        values.mip_pending = true;
        throw SpriteMipsPending{};
    }
    // Upload only after the complete CPU bank is available. Partial GPU admission
    // stays detached; failure cannot replace any member of the retained draw.
    for (auto& [key, frame] : bank.frames) {
        (void)key;
        if (!frame.image)
            frame.image = host_->gpu_textures_.acquire(frame.pixels);
    }
    bank.complete = true;
    return values.mip_candidate;
}
} // namespace forge
