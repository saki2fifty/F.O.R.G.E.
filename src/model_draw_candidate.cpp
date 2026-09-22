#include "model_draw_candidate.hpp"
#include "material_slot.hpp"
#include "pbr_material.hpp"
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
ModelDrawCandidate::ModelDrawCandidate(std::filesystem::path project,
                                       std::shared_ptr<const AssetCatalog> catalog,
                                       std::uint64_t epoch, AssetRef<MeshAsset> mesh,
                                       std::vector<MaterialSlotOverride> overrides,
                                       ResourcePool<MeshAsset>& meshes,
                                       std::shared_ptr<const MaterialPreviewSelection> preview)
    : epoch_(epoch), project_(std::move(project)), catalog_(std::move(catalog)),
      preview_(std::move(preview)), overrides_(std::move(overrides)) {
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
void ModelDrawCandidate::fail(ResourceState state, std::string message) {
    state_ = state;
    diagnostic_ = std::move(message);
    prepared_ = {};
    mesh_ = {};
    materials_.clear();
    textures_.clear();
    catalog_.reset();
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
            prepared_.selection = select_mesh_materials(prepared_.mesh.get(), overrides_);
            // Missing authored slots remain visible diagnostics, never guessed
            // replacement bindings. Existing slots still use their selected values.
            for (const auto& binding : prepared_.selection.bindings)
                if (binding.material.id && !materials_.contains(binding.material.id)) {
                    if (preview_ && binding.material == preview_->asset)
                        materials_.emplace(
                            binding.material.id,
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
                        materials_.emplace(binding.material.id,
                                           request_model_pbr_material(materials, project_, catalog_,
                                                                      binding.material));
                }
            stage_ = 1;
        }
        if (stage_ == 1) {
            bool ready = true;
            for (const auto& [id, ticket] : materials_)
                ready = acquire_ready(materials, ticket, prepared_.materials[id]) && ready;
            if (!ready)
                return;
            for (const auto& [id, material] : prepared_.materials) {
                (void)id;
                for (const auto& [role, ref] : material->textures) {
                    const auto semantic = material->values.textures.at(role).semantic;
                    DrawTextureKey key{ref.id, semantic};
                    if (!textures_.contains(key))
                        textures_.emplace(
                            key, request_texture(textures, project_, catalog_, ref, semantic));
                }
            }
            stage_ = 2;
        }
        bool ready = true;
        for (const auto& [key, ticket] : textures_)
            ready = acquire_ready(textures, ticket, prepared_.textures[key]) && ready;
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
