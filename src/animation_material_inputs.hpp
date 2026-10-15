#pragma once
#include "model_draw_candidate.hpp"
#include <forge/render_components.hpp>
#include <forge/runtime_resource_service.hpp>
#include <map>
#include <memory>
#include <set>
namespace forge {
// A private animation consumer's bounded subscriptions into the existing CPU
// Resources service. Owns no resource pool, catalog, clock or authored state.
struct AnimationMaterialPending : std::runtime_error {
    using std::runtime_error::runtime_error;
};
class AnimationMaterialInputs {
  public:
    ~AnimationMaterialInputs() { clear(); }
    void begin(const std::shared_ptr<RuntimeResourceService>& owner) {
        if (owner_.lock() != owner) {
            clear();
            owner_ = owner;
        }
        used_.clear();
    }
    asset_detail::PreparedModelDraw select(const MeshRenderer& renderer) {
        const auto owner = owner_.lock();
        if (!owner)
            throw std::runtime_error("Material animation requires runtime CPU Resources");
        asset_detail::PreparedModelDraw result;
        const auto mesh = request(RuntimeResourceKind::Mesh, renderer.mesh.id);
        require_ready(*owner, mesh);
        result.mesh = owner->mesh(mesh);
        if (!result.mesh)
            throw AnimationMaterialPending("Waiting for animated material Mesh data");
        result.selection =
            select_mesh_materials(result.mesh.get(), renderer.materials, renderer.material_variant);
        if (!result.selection.unresolved.empty())
            throw std::runtime_error("Animated material Mesh slot selection is unresolved");
        std::set<AssetId> materials;
        for (const auto& lod : result.selection.parts)
            for (const auto& material : lod)
                if (material.id)
                    materials.insert(material.id);
        bool pending = false;
        for (const auto asset : materials) {
            try {
                const auto token = request(RuntimeResourceKind::Material, asset);
                require_ready(*owner, token);
                auto material = owner->material(token);
                if (!material)
                    throw AnimationMaterialPending("Waiting for animated Material data");
                result.materials.emplace(asset, std::move(material));
            } catch (const AnimationMaterialPending&) {
                // Request all unique selected materials at this boundary so
                // readiness does not serialize one file load per fixed tick.
                pending = true;
            }
        }
        if (pending)
            throw AnimationMaterialPending("Waiting for selected animated Materials");
        return result;
    }
    void finish() {
        const auto owner = owner_.lock();
        for (auto at = tokens_.begin(); at != tokens_.end();) {
            if (used_.contains(at->first)) {
                ++at;
                continue;
            }
            if (owner)
                try {
                    owner->release(at->second);
                } catch (...) { /* provider may already be retired */
                }
            at = tokens_.erase(at);
        }
        if (!owner)
            return;
        // Discover first, then retire unused subscriptions before allocating
        // replacements. A fully occupied old selection must not block a valid
        // replacement whose complete demand still fits the same shared limit.
        for (const auto& key : used_) {
            if (tokens_.contains(key))
                continue;
            const auto token = owner->request(key.first, key.second);
            try {
                tokens_.emplace(key, token);
            } catch (...) {
                owner->release(token);
                throw;
            }
        }
    }
    void clear() noexcept {
        if (const auto owner = owner_.lock())
            for (const auto& [key, token] : tokens_)
                try {
                    owner->release(token);
                } catch (...) { /* provider may already be retired */
                }
        tokens_.clear();
        used_.clear();
        owner_.reset();
    }

  private:
    using Key = std::pair<RuntimeResourceKind, AssetId>;
    std::weak_ptr<RuntimeResourceService> owner_;
    std::map<Key, std::uint64_t> tokens_;
    std::set<Key> used_;
    std::uint64_t request(RuntimeResourceKind kind, AssetId asset) {
        const Key key{kind, asset};
        if (!used_.contains(key) && used_.size() == 256)
            throw std::runtime_error("Material animation unique resource limit (256) exceeded");
        used_.insert(key);
        if (const auto found = tokens_.find(key); found != tokens_.end())
            return found->second;
        throw AnimationMaterialPending("Waiting for animated material resource subscription");
    }
    static void require_ready(const RuntimeResourceService& owner, std::uint64_t token) {
        const auto status = owner.inspect(token);
        if (status.state == "failed" || status.requested_revision.empty())
            throw std::runtime_error("Animated material resource admission failed: " +
                                     status.diagnostic);
        if (status.state != "ready" || status.requested_revision != status.retained_revision)
            throw AnimationMaterialPending("Waiting for selected animated material revision");
    }
};
} // namespace forge
