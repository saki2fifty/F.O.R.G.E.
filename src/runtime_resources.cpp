#include "model_render_resource.hpp"
#include <forge/engine_assets.hpp>
#include <forge/runtime_resources.hpp>
#include <forge/shader_resource.hpp>
#include <future>
#include <limits>
#include <variant>
namespace forge {
namespace {
using Lease = std::variant<ResourceLease<MeshAsset>, ResourceLease<MaterialAsset>,
                           ResourceLease<TextureAsset>, ResourceLease<ShaderAsset>>;
struct Subscription {
    RuntimeResourceKind kind;
    AssetId asset;
    RuntimeTextureVariant variant;
    ResourceTicket ticket;
    Lease retained;
    std::string selection_error;
};
class RuntimeResources final : public RuntimeResourceService {
    const std::thread::id owner_ = std::this_thread::get_id();
    std::filesystem::path project_;
    ServiceAccess services_;
    std::shared_ptr<const AssetCatalog> catalog_;
    // Aggregate CPU budget512MiB; each family has one preparation worker.
    ResourcePool<MeshAsset> meshes_{{1, 64, 256, 128ull * 1024 * 1024}};
    ResourcePool<MaterialAsset> materials_{{1, 64, 256, 128ull * 1024 * 1024}};
    ResourcePool<TextureAsset> textures_{{1, 64, 256, 128ull * 1024 * 1024}};
    ResourcePool<ShaderAsset> shaders_{{1, 64, 256, 128ull * 1024 * 1024}};
    std::map<std::uint64_t, Subscription> subscriptions_;
    bool refresh_again_ = false, alive_ = true;
    // Last member: join file-only work before any owner state is destroyed.
    std::future<std::shared_ptr<const AssetCatalog>> refresh_;
    void check() const {
        if (owner_ != std::this_thread::get_id() || !alive_)
            throw std::runtime_error("Runtime resources require their owner thread");
    }
    ResourceTicket select(RuntimeResourceKind kind, AssetId asset, RuntimeTextureVariant variant) {
        if (unsigned(variant) > unsigned(RuntimeTextureVariant::HdrColor) ||
            (kind != RuntimeResourceKind::Texture && variant != RuntimeTextureVariant::Automatic))
            throw std::runtime_error("Texture variant is invalid for this resource request");
        const char* expected = nullptr;
        switch (kind) {
        case RuntimeResourceKind::Mesh:
            expected = MeshAsset::type;
            break;
        case RuntimeResourceKind::Material:
            expected = MaterialAsset::type;
            break;
        case RuntimeResourceKind::Texture:
            expected = TextureAsset::type;
            break;
        case RuntimeResourceKind::Shader:
            expected = ShaderAsset::type;
            break;
        default:
            throw std::runtime_error("Unsupported runtime resource type");
        }
        if (const auto* builtin = engine_asset(asset)) {
            if (std::string_view(builtin->type) != expected)
                throw std::runtime_error("Runtime resource asset type mismatch");
        } else {
            const auto found = catalog_->records().find(asset);
            if (found == catalog_->records().end() || found->second.type != expected ||
                (found->second.subasset && found->second.subasset->removed))
                throw std::runtime_error(
                    "Runtime resource is missing, removed or has the wrong type");
        }
        switch (kind) {
        case RuntimeResourceKind::Mesh:
            return asset_detail::request_model_mesh(meshes_, project_, catalog_, {asset});
        case RuntimeResourceKind::Material:
            return asset_detail::request_model_pbr_material(materials_, project_, catalog_,
                                                            {asset});
        case RuntimeResourceKind::Texture: {
            std::optional<TextureSemantic> semantic;
            switch (variant) {
            case RuntimeTextureVariant::Automatic:
                break;
            case RuntimeTextureVariant::Color:
                semantic = TextureSemantic::Color;
                break;
            case RuntimeTextureVariant::Data:
                semantic = TextureSemantic::Data;
                break;
            case RuntimeTextureVariant::Normal:
                semantic = TextureSemantic::Normal;
                break;
            case RuntimeTextureVariant::HdrColor:
                semantic = TextureSemantic::HdrColor;
                break;
            }
            return asset_detail::request_texture(textures_, project_, catalog_, {asset}, semantic);
        }
        case RuntimeResourceKind::Shader:
            return request_shader(shaders_, project_, *catalog_, {asset});
        }
        throw std::runtime_error("Unsupported runtime resource type");
    }
    template <class T> void retain(ResourcePool<T>& pool, Subscription& value) {
        if (auto lease = pool.acquire(value.ticket))
            value.retained = std::move(lease);
    }

  public:
    RuntimeResources(std::filesystem::path project, ServiceAccess services)
        : project_(std::move(project)), services_(services),
          catalog_(std::make_shared<const AssetCatalog>(AssetCatalog::open_project(project_))) {}
    void close() {
        check();
        alive_ = false;
        if (refresh_.valid())
            refresh_.wait();
        subscriptions_.clear();
        meshes_.close();
        materials_.close();
        textures_.close();
        shaders_.close();
    }
    std::uint64_t request(RuntimeResourceKind kind, AssetId asset,
                          RuntimeTextureVariant variant) override {
        check();
        if (subscriptions_.size() >= 256)
            throw std::runtime_error("Runtime resource subscription limit256 reached");
        auto ticket = select(kind, asset, variant);
        // Unique across providers/worlds/modules, without exposing a pointer.
        static std::atomic<std::uint64_t> next{1};
        auto token = next.load();
        do {
            if (token == std::numeric_limits<std::uint64_t>::max())
                throw std::runtime_error("Runtime resource token space exhausted");
        } while (!next.compare_exchange_weak(token, token + 1));
        subscriptions_.emplace(token,
                               Subscription{kind, asset, variant, std::move(ticket), {}, {}});
        return token;
    }
    RuntimeResourceStatus inspect(std::uint64_t token) const override {
        check();
        const auto found = subscriptions_.find(token);
        if (found == subscriptions_.end())
            throw std::runtime_error("Unknown or released runtime resource subscription");
        const auto& value = found->second;
        const auto info = value.ticket.inspect();
        RuntimeResourceStatus result{resource_state_name(info.state),
                                     info.identity.revision,
                                     {},
                                     info.diagnostic,
                                     info.source_generation};
        std::visit(
            [&](const auto& lease) {
                if (lease)
                    result.retained_revision = lease.identity().revision;
            },
            value.retained);
        if (!value.selection_error.empty()) {
            result.state = "failed";
            result.requested_revision.clear(); // No valid candidate was selected.
            result.source_generation = 0;
            result.diagnostic = value.selection_error;
        }
        return result;
    }
    bool release(std::uint64_t token) override {
        check();
        // Release only this observer. Shared preparation/other leases remain valid.
        return subscriptions_.erase(token) != 0;
    }
    void refresh() override {
        check();
        if (refresh_.valid()) {
            refresh_again_ = true;
            return;
        }
        refresh_ = std::async(std::launch::async, [root = project_] {
            return std::make_shared<const AssetCatalog>(AssetCatalog::open_project(root));
        });
    }
    void synchronize() override {
        check();
        if (refresh_.valid() &&
            refresh_.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
            try {
                catalog_ = refresh_.get();
                for (auto& [token, value] : subscriptions_) {
                    (void)token;
                    try {
                        auto candidate = select(value.kind, value.asset, value.variant);
                        value.ticket = std::move(candidate);
                        value.selection_error.clear();
                    } catch (const std::exception& e) {
                        value.selection_error = e.what();
                    }
                }
            } catch (const std::exception& e) {
                services_.emit({Severity::Error, "resources.catalog", e.what(), {}});
            }
            if (std::exchange(refresh_again_, false))
                refresh();
        }
        meshes_.pump();
        materials_.pump();
        textures_.pump();
        shaders_.pump();
        for (auto& [token, value] : subscriptions_) {
            (void)token;
            if (!value.selection_error.empty())
                continue;
            switch (value.kind) {
            case RuntimeResourceKind::Mesh:
                retain(meshes_, value);
                break;
            case RuntimeResourceKind::Material:
                retain(materials_, value);
                break;
            case RuntimeResourceKind::Texture:
                retain(textures_, value);
                break;
            case RuntimeResourceKind::Shader:
                retain(shaders_, value);
                break;
            }
        }
    }
};
} // namespace
EngineModule runtime_resources_module(std::filesystem::path project) {
    EngineModule module;
    module.id = "forge.resources";
    module.dependencies = {"forge.core"};
    module.runtime_roles = role_mask(WorldRole::Runtime);
    module.allowed_services =
        capability(Capability::Diagnostics) | capability(Capability::Resources);
    module.provided_services = capability(Capability::Resources);
    module.start = [project = std::move(project)](ModuleContext& context) {
        auto owner = std::make_shared<RuntimeResources>(project, context.services);
        context.services.publish_resources(owner);
        context.state = std::move(owner);
    };
    module.stop = [](ModuleContext& context) {
        context.services.publish_resources({});
        if (context.state)
            std::static_pointer_cast<RuntimeResources>(context.state)->close();
        context.state.reset();
    };
    return module;
}
} // namespace forge
