#include "model_draw_candidate.hpp"
#include "engine_render_resource.hpp"
#include "material_slot.hpp"
#include "pbr_material.hpp"
#include <set>
namespace forge::asset_detail {
namespace {
template <class T>
bool acquire_ready(ResourcePool<T>& pool, const ResourceTicket& ticket, ResourceLease<T>& lease) {
    if (lease) {
        (void)lease.get();
        return true;
    }
    const auto info = ticket.inspect();
    switch (info.state) {
    case ResourceState::Ready:
        lease = pool.acquire(ticket);
        if (!lease)
            throw std::runtime_error("Selected draw resource revision is no longer available");
        return true;
    case ResourceState::Queued:
    case ResourceState::DependencyPending:
    case ResourceState::Loading:
    case ResourceState::Replacing:
        return false;
    default:
        throw std::runtime_error("Draw resource " + info.identity.asset.str() + ": " +
                                 resource_state_name(info.state) + " " + info.diagnostic);
    }
}
} // namespace
ModelDrawCandidate::ModelDrawCandidate(
    std::filesystem::path project, std::shared_ptr<const AssetCatalog> catalog, std::uint64_t epoch,
    AssetRef<MeshAsset> mesh, std::vector<MaterialSlotOverride> overrides,
    ResourcePool<MeshAsset>& meshes, std::shared_ptr<const MaterialPreviewSelection> preview,
    AssetRef<MaterialVariantAsset> variant, bool initial_fallbacks)
    : epoch_(epoch), project_(std::move(project)), catalog_(std::move(catalog)),
      preview_(std::move(preview)), overrides_(std::move(overrides)), variant_(variant),
      initial_fallbacks_(initial_fallbacks) {
    if (!epoch_ || !catalog_ || !mesh.id)
        throw std::runtime_error("Invalid complete model draw request");
    detail::validate_material_slots(overrides_);
    if (preview_) {
        if (!preview_->asset.id || !preview_->generation ||
            !valid_content_digest(preview_->revision))
            throw std::runtime_error("Invalid material preview selection");
        validate_render_material(preview_->data);
    }
    mesh_ = request_model_mesh(meshes, project_, catalog_, mesh);
}
void ModelDrawCandidate::check_thread() const {
    if (thread_ != std::this_thread::get_id())
        throw std::runtime_error("Model draw candidate accessed from another thread");
}
void ModelDrawCandidate::note(const std::string& text) {
    if (diagnostic_.size() >= 4096)
        return;
    if (!diagnostic_.empty())
        diagnostic_ += "\n";
    diagnostic_.append(text, 0, 4096 - diagnostic_.size());
}
void ModelDrawCandidate::fallback_material(AssetId requested, const std::string& why,
                                           ResourcePool<MaterialAsset>& materials) {
    const auto fallback = engine_material(EngineMaterial::Error);
    if (!initial_fallbacks_ || requested == fallback.id)
        throw std::runtime_error(why);
    note("Material " + requested.str() + " uses the error surface: " + why);
    prepared_.material_fallbacks[requested] = fallback.id;
    prepared_.materials.erase(requested);
    materials_.try_emplace(fallback.id, request_engine_material(materials, fallback));
}
void ModelDrawCandidate::fallback_texture(DrawTextureKey requested, const std::string& why,
                                          ResourcePool<TextureAsset>& textures) {
    if (!initial_fallbacks_ || engine_texture_asset(requested.first))
        throw std::runtime_error(why);
    const auto semantic = requested.second;
    const auto kind = semantic == TextureSemantic::Normal     ? EngineTexture::FlatNormal
                      : semantic == TextureSemantic::Color    ? EngineTexture::Checker
                      : semantic == TextureSemantic::HdrColor ? EngineTexture::Black
                                                              : EngineTexture::White;
    const auto fallback = engine_texture(kind, texture_dimensions_.at(requested));
    const DrawTextureKey key{fallback.id, semantic};
    note("Texture " + requested.first.str() + " uses an engine fallback: " + why);
    prepared_.texture_fallbacks[requested] = key;
    prepared_.textures.erase(requested);
    textures_.try_emplace(key, request_engine_texture(textures, fallback, semantic));
}
bool ModelDrawCandidate::error_surface(std::string diagnostic,
                                       ResourcePool<MaterialAsset>& materials) {
    check_thread();
    if (!initial_fallbacks_ || error_surface_retry_ || state_ != ResourceState::Ready)
        return false;
    error_surface_retry_ = true;
    note("Initial GPU surface failed; using the error surface: " + diagnostic);
    const auto fallback = engine_material(EngineMaterial::Error);
    const auto ticket = request_engine_material(materials, fallback);
    for (const auto& lod : prepared_.selection.parts)
        for (const auto& material : lod)
            prepared_.material_fallbacks[material.id] = fallback.id;
    prepared_.materials.clear();
    prepared_.textures.clear();
    prepared_.texture_fallbacks.clear();
    materials_.clear();
    textures_.clear();
    texture_dimensions_.clear();
    materials_.emplace(fallback.id, ticket);
    state_ = ResourceState::Loading;
    stage_ = 1;
    return true;
}
void ModelDrawCandidate::fail(ResourceState state, std::string message) {
    state_ = state;
    diagnostic_ = std::move(message);
    prepared_ = {};
    mesh_ = {};
    materials_.clear();
    textures_.clear();
    catalog_.reset();
    texture_dimensions_.clear();
    preview_.reset();
}
void ModelDrawCandidate::advance(std::uint64_t epoch, ResourcePool<MeshAsset>& meshes,
                                 ResourcePool<MaterialAsset>& materials,
                                 ResourcePool<TextureAsset>& textures) {
    check_thread();
    if (state_ != ResourceState::Loading && state_ != ResourceState::Ready)
        return;
    if (epoch != epoch_) {
        fail(ResourceState::Stale, "Catalog publication changed before draw adoption");
        return;
    }
    if (state_ == ResourceState::Ready)
        return;
    try {
        if (stage_ == 0) {
            if (!acquire_ready(meshes, mesh_, prepared_.mesh))
                return;
            prepared_.selection = select_mesh_materials(prepared_.mesh.get(), overrides_, variant_);
            // Missing authored slots remain visible diagnostics, never guessed
            // replacement bindings. Existing slots still use their selected values.
            std::set<AssetId> required;
            for (const auto& lod : prepared_.selection.parts)
                for (const auto& material : lod)
                    if (material.id)
                        required.insert(material.id);
            for (const auto id : required) {
                const AssetRef<MaterialAsset> material{id};
                try {
                    if (preview_ && material == preview_->asset)
                        materials_.emplace(
                            material.id,
                            materials.request(
                                preview_->asset, preview_->revision, preview_->generation,
                                [selected = preview_](std::stop_token stop) {
                                    if (stop.stop_requested())
                                        throw std::runtime_error("Material preview cancelled");
                                    auto data =
                                        std::make_unique<MaterialResourceData>(selected->data);
                                    const auto bytes = data->resident_bytes();
                                    return ResourceCandidate<MaterialAsset>{std::move(data),
                                                                            {bytes}};
                                },
                                {}, 0, "builtin:material-preview-v1"));
                    else
                        materials_.emplace(
                            material.id,
                            request_model_pbr_material(materials, project_, catalog_, material));
                } catch (const std::exception& e) {
                    fallback_material(id, e.what(), materials);
                }
            }
            stage_ = 1;
        }
        if (stage_ == 1) {
            bool ready = true;
            for (auto it = materials_.begin(); it != materials_.end();) {
                try {
                    ready = acquire_ready(materials, it->second, prepared_.materials[it->first]) &&
                            ready;
                    ++it;
                } catch (const std::exception& e) {
                    const auto id = it->first;
                    it = materials_.erase(it);
                    fallback_material(id, e.what(), materials);
                    ready = false;
                }
            }
            if (!ready)
                return;
            for (const auto& [id, material] : prepared_.materials) {
                (void)id;
                for (const auto& [role, ref] : material->textures) {
                    const auto semantic = material->values.textures.at(role).semantic;
                    DrawTextureKey key{ref.id, semantic};
                    const auto dimension = material->values.textures.at(role).dimension;
                    const auto [found, fresh] = texture_dimensions_.emplace(key, dimension);
                    if (!fresh && found->second != dimension)
                        throw std::runtime_error(
                            "One texture reference requires incompatible dimensions");
                    if (!textures_.contains(key) && !prepared_.texture_fallbacks.contains(key))
                        try {
                            textures_.emplace(
                                key, request_texture(textures, project_, catalog_, ref, semantic));
                        } catch (const std::exception& e) {
                            fallback_texture(key, e.what(), textures);
                        }
                }
            }
            stage_ = 2;
        }
        bool ready = true;
        for (auto it = textures_.begin(); it != textures_.end();) {
            try {
                ready = acquire_ready(textures, it->second, prepared_.textures[it->first]) && ready;
                ++it;
            } catch (const std::exception& e) {
                const auto key = it->first;
                it = textures_.erase(it);
                fallback_texture(key, e.what(), textures);
                ready = false;
            }
        }
        if (ready)
            state_ = ResourceState::Ready;
    } catch (const std::exception& e) {
        fail(ResourceState::DependencyFailed, e.what());
    }
}
void ModelDrawCandidate::cancel() {
    check_thread();
    fail(ResourceState::Cancelled, "Draw preparation cancelled");
}
ResourceState ModelDrawCandidate::state() const {
    check_thread();
    return state_;
}
const std::string& ModelDrawCandidate::diagnostic() const {
    check_thread();
    return diagnostic_;
}
const PreparedModelDraw* ModelDrawCandidate::ready() const {
    check_thread();
    return state_ == ResourceState::Ready ? &prepared_ : nullptr;
}
} // namespace forge::asset_detail
