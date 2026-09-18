#include <algorithm>
#include <forge/assets.hpp>
#include <forge/runtime_ui.hpp>
namespace forge {
UiRuntime::UiRuntime(std::filesystem::path project, ServiceAccess services)
    : services_(services), project_(std::move(project)) {}
void UiRuntime::check() const {
    if (owner_ != std::this_thread::get_id() || !active_)
        throw std::runtime_error("UI service requires its live owner thread");
}
void UiRuntime::publish(EntityId entity, const std::string& name, const Json& value) {
    check();
    if (!active_ || !entity || !ui_protocol::identifier(name) || name == "tick" || name == "paused")
        throw std::runtime_error("Invalid/reserved UI model publication");
    ui_protocol::validate_value(value);
    if (!models_.contains(entity) && models_.size() >= ui_protocol::max_documents)
        throw std::runtime_error("UI model document limit exceeded");
    auto next = models_.contains(entity) ? models_.at(entity) : Json::object();
    next[name] = value;
    ui_protocol::validate_model(next);
    if (next.size() > ui_protocol::max_values - 2)
        throw std::runtime_error("UI model reserved values limit");
    std::size_t total = next.dump().size();
    for (const auto& [id, model] : models_)
        if (id != entity)
            total += model.dump().size();
    if (total > 16 * 1024)
        throw std::runtime_error("UI copied model exceeds aggregate 16 KiB limit");
    models_[entity] = std::move(next);
}
void UiRuntime::allow_action(const std::string& name) {
    check();
    if (!active_ || !ui_protocol::identifier(name) || name == "Pause" || name == "Resume" ||
        name == "Step")
        throw std::runtime_error("Invalid/reserved UI command capability");
    if (!actions_.contains(name) && actions_.size() >= 29)
        throw std::runtime_error("UI command capability limit");
    actions_.insert(name);
}
std::optional<UiAction> UiRuntime::poll_action(const std::string& name) {
    check();
    if (!active_)
        throw std::runtime_error("UI provider has stopped");
    auto it = std::find_if(pending_.begin(), pending_.end(),
                           [&](const auto& action) { return action.command == name; });
    if (it == pending_.end())
        return std::nullopt;
    auto result = std::move(*it);
    pending_.erase(it);
    return result;
}
Json UiRuntime::snapshot(const Scene& scene, const std::string& session, std::uint64_t generation,
                         std::uint64_t tick, bool paused) {
    check();
    auto profile = services_.profile("ui", "ModelSnapshot", tick);
    Json docs = Json::array(), errors = Json::array();
    std::optional<AssetCatalog> catalog;
    try {
        catalog.emplace(AssetCatalog::open_project(project_));
    } catch (const std::exception& e) {
        errors.push_back({{"error", std::string(e.what()).substr(0, 256)}});
    }
    std::set<EntityId> live, documents;
    const auto document = scene.effective_document();
    for (const auto& e : document.at("entities")) {
        const auto entity = e.at("id").get<EntityId>();
        live.insert(entity);
        const auto& components = e.at("components");
        if (e.value("prefab", false) || !components.contains("forge.ui_document"))
            continue;
        const auto& c = components.at("forge.ui_document");
        if (!c.at("enabled").get<bool>() || c.at("document").is_null())
            continue;
        const auto asset = c.at("document").get<AssetId>();
        const auto resolution =
            catalog ? catalog->resolve(asset, UiDocumentAsset::type) : AssetResolution{};
        if (resolution.state != AssetState::Available || resolution.record->schema_version != 1) {
            errors.push_back(
                {{"entity", entity},
                 {"asset", asset},
                 {"error", resolution.diagnostic.empty() ? "Unsupported UI asset schema"
                                                         : resolution.diagnostic.substr(0, 256)}});
        }
        if (docs.size() >= ui_protocol::max_documents) {
            errors.push_back({{"error", "At most 16 UI documents can be active"}});
            break;
        }
        documents.insert(entity);
        const auto handle = scene.entity(entity.str()).id();
        auto it = instances_.find(entity);
        if (it == instances_.end() || it->second.asset != asset || it->second.entity != handle)
            instances_[entity] = {asset, handle,
                                  entity.str() + ":" + std::to_string(++next_instance_)};
        auto model = models_.contains(entity) ? models_.at(entity) : Json::object();
        model["tick"] = tick;
        model["paused"] = paused;
        Json actions = {"Pause", "Resume", "Step"};
        for (const auto& a : actions_)
            actions.push_back(a);
        docs.push_back({{"entity", entity},
                        {"asset", asset},
                        {"instance", instances_.at(entity).identity},
                        {"visible", c.at("visible")},
                        {"layer", c.at("layer")},
                        {"model", std::move(model)},
                        {"commands", std::move(actions)}});
    }
    std::erase_if(instances_, [&](const auto& entry) { return !documents.contains(entry.first); });
    std::erase_if(models_, [&](const auto& entry) { return !live.contains(entry.first); });
    std::erase_if(pending_, [&](const auto& action) { return !live.contains(action.entity); });
    Json result = {{"version", ui_protocol::version}, {"session", session},
                   {"generation", generation},        {"revision", ++revision_},
                   {"documents", std::move(docs)},    {"errors", std::move(errors)}};
    ui_protocol::validate_snapshot(result);
    return result;
}
void UiRuntime::command(const Scene& scene, const Json& request,
                        const std::function<void(const std::string&)>& control) {
    check();
    auto profile = services_.profile("ui", "CommandValidation");
    const auto instance = request.at("instance").get<std::string>();
    if (instance.size() < 38 || instance[36] != ':')
        throw std::runtime_error("Invalid UI document instance");
    const auto entity = EntityId::parse(instance.substr(0, 36));
    const auto known = instances_.find(entity);
    if (known == instances_.end() || known->second.identity != instance)
        throw std::runtime_error("Stale UI document instance");
    const auto asset = known->second.asset;
    auto e = scene.entity(entity.str());
    if (!e.is_valid() || e.id() != known->second.entity || e.has(flecs::Prefab) ||
        !e.has<UiDocument>())
        throw std::runtime_error("UI document no longer exists");
    const auto& d = e.get<UiDocument>();
    if (!d.enabled || !d.visible || d.document.id != asset)
        throw std::runtime_error("UI document no longer accepts input");
    if (AssetCatalog::open_project(project_).resolve(d.document).state != AssetState::Available)
        throw std::runtime_error("UI document asset is unavailable");
    const auto name = request.at("command").get<std::string>();
    if (request.contains("value"))
        throw std::runtime_error("Registered UI actions currently accept no arguments");
    if (name == "Pause" || name == "Resume" || name == "Step") {
        control(name);
        return;
    }
    if (!actions_.contains(name))
        throw std::runtime_error("UI command capability not registered");
    if (pending_.size() >= 128)
        throw std::runtime_error("UI action queue is full");
    pending_.push_back({entity, name, request.value("value", Json())});
}
void UiRuntime::shutdown() {
    if (owner_ != std::this_thread::get_id())
        throw std::runtime_error("UI shutdown requires owner thread");
    active_ = false;
    instances_.clear();
    models_.clear();
    actions_.clear();
    pending_.clear();
}
EngineModule ui_module(std::filesystem::path project) {
    auto m = ui_schema_module();
    m.runtime_roles = role_mask(WorldRole::Runtime);
    m.provided_services = capability(Capability::Ui);
    m.allowed_services = m.provided_services | capability(Capability::Profiling) |
                         capability(Capability::Diagnostics);
    m.start = [project = std::move(project)](ModuleContext& c) {
        auto r = std::make_shared<UiRuntime>(project, c.services);
        c.services.publish_ui(r);
        c.state = r;
    };
    m.stop = [](ModuleContext& c) {
        std::static_pointer_cast<UiRuntime>(c.state)->shutdown();
        c.services.publish_ui({});
    };
    return m;
}
} // namespace forge
