#include <algorithm>
#include <charconv>
#include <forge/application_states.hpp>
#include <forge/assets.hpp>
#include <forge/runtime_ui.hpp>
namespace forge {
namespace {
struct SceneLocalization {
    Json descriptor;
    std::string scope;
};
std::optional<SceneLocalization> scene_localization(const Scene& scene, const Json& rows) {
    std::optional<SceneLocalization> result;
    for (const auto& row : rows) {
        if (row.value("prefab", false) || !row.at("components").contains("forge.ui_localization"))
            continue;
        const auto& value = row.at("components").at("forge.ui_localization");
        if (value.at("catalog").is_null())
            continue; // An unassigned component has no UI resource demand.
        if (result)
            throw std::runtime_error(
                "One Scene supports one configured UI Localization controller");
        const auto entity = row.at("id").get<EntityId>();
        const auto native = scene.entity(entity.str());
        if (!native)
            throw std::runtime_error("UI Localization controller no longer exists");
        Json descriptor{
            {"entity", entity}, {"asset", value.at("catalog")}, {"locale", value.at("locale")}};
        ui_protocol::validate_localization(descriptor);
        result = SceneLocalization{std::move(descriptor),
                                   entity.str() + ":" + std::to_string(native.id()) + ":" +
                                       value.at("catalog").get<std::string>()};
    }
    return result;
}
struct SceneStates {
    Json descriptor;
    std::string scope;
};
std::optional<SceneStates> scene_states(const Scene& scene, const Json& rows) {
    auto descriptor = application_states_status(scene.world());
    if (descriptor.is_null())
        return std::nullopt;
    std::string scope = descriptor.at("definition_digest").get<std::string>();
    descriptor["controller"] = nullptr;
    for (const auto& row : rows) {
        if (row.value("prefab", false) ||
            !row.at("components").contains("forge.application_states"))
            continue;
        const auto& definition =
            row.at("components").at("forge.application_states").at("definition");
        if (definition.is_null())
            continue;
        if (!descriptor.at("controller").is_null() || definition != descriptor.at("source_asset"))
            throw std::runtime_error(
                "UI application state controller differs from the admitted definition");
        const auto entity = row.at("id").get<EntityId>();
        const auto native = scene.entity(entity.str());
        if (!native)
            throw std::runtime_error("UI application state controller no longer exists");
        descriptor["controller"] = entity;
        scope += ":" + entity.str() + ":" + std::to_string(native.id());
    }
    if (!descriptor.at("source_asset").is_null() && descriptor.at("controller").is_null())
        throw std::runtime_error("UI application state controller was removed; restart Play");
    ui_protocol::validate_application_states(descriptor);
    return SceneStates{std::move(descriptor), std::move(scope)};
}
} // namespace
UiRuntime::UiRuntime(std::filesystem::path project, ServiceAccess services)
    : services_(services), project_(std::move(project)) {}
void UiRuntime::check() const {
    if (owner_ != std::this_thread::get_id() || !active_)
        throw std::runtime_error("UI service requires its live owner thread");
}
void UiRuntime::publish(EntityId entity, const std::string& name, const Json& value) {
    check();
    if (!active_ || !entity || !ui_protocol::identifier(name) || name == "tick" ||
        name == "paused" || name.starts_with("forge_loading_"))
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
        name == "Step" || name == "TimeScale")
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
void UiRuntime::allow_value_action(const std::string& name) {
    allow_action(name);
    value_actions_.insert(name);
}
Json UiRuntime::snapshot(const Scene& scene, const std::string& session, std::uint64_t generation,
                         std::uint64_t tick, bool paused) {
    check();
    auto profile = services_.profile("ui", "ModelSnapshot", tick);
    Json docs = Json::array(), errors = Json::array();
    std::optional<AssetCatalog> catalog;
    bool catalog_requested = false;
    std::set<EntityId> live, documents;
    auto conditions = scene.state_conditions();
    const auto rows = scene.ui_document_rows();
    const bool demand = std::any_of(rows.begin(), rows.end(), [&](const auto& row) {
        const auto& components = row.at("components");
        return !row.value("prefab", false) && components.contains("forge.ui_document") &&
               components.at("forge.ui_document").at("enabled").template get<bool>() &&
               !components.at("forge.ui_document").at("document").is_null() &&
               conditions.active(scene.entity(row.at("id").template get<std::string>()).id());
    });
    const auto localization = demand ? scene_localization(scene, rows) : std::nullopt;
    const auto states = demand ? scene_states(scene, rows) : std::nullopt;
    const auto scope = localization ? localization->scope : std::string{};
    const auto state_scope = states ? states->scope : std::string{};
    if (localization && actions_.contains("forge.locale"))
        throw std::runtime_error("Localized UI conflicts with a custom forge.locale action");
    if (states && actions_.contains("forge.state"))
        throw std::runtime_error("Application state UI conflicts with a custom forge.state action");
    for (const auto& e : rows) {
        const auto entity = e.at("id").get<EntityId>();
        live.insert(entity);
        const auto& components = e.at("components");
        if (e.value("prefab", false) || !components.contains("forge.ui_document"))
            continue;
        const auto& c = components.at("forge.ui_document");
        if (!c.at("enabled").get<bool>() || c.at("document").is_null() ||
            !conditions.active(scene.entity(entity.str()).id()))
            continue;
        if (!catalog_requested) {
            catalog_requested = true;
            try {
                catalog.emplace(AssetCatalog::open_project(project_));
            } catch (const std::exception& e) {
                errors.push_back({{"error", std::string(e.what()).substr(0, 256)}});
            }
        }
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
        if (it == instances_.end() || it->second.asset != asset || it->second.entity != handle ||
            it->second.localization_scope != scope ||
            it->second.application_state_scope != state_scope)
            instances_[entity] = {asset, handle,
                                  entity.str() + ":" + std::to_string(++next_instance_), scope,
                                  state_scope};
        auto model = models_.contains(entity) ? models_.at(entity) : Json::object();
        if (localization &&
            (model.contains("forge_locale") || model.contains("forge_resolved_locale")))
            throw std::runtime_error("Localized UI reserves its locale presentation bindings");
        if (states)
            for (const auto& [key, value] : model.items()) {
                (void)value;
                if (key.starts_with("forge_state_") || key.starts_with("forge_rev_"))
                    throw std::runtime_error(
                        "Application state UI reserves its state presentation bindings");
            }
        model["tick"] = tick;
        model["paused"] = paused;
        Json actions = {"Pause", "Resume", "Step", "TimeScale"};
        if (localization)
            actions.push_back("forge.locale");
        if (states)
            actions.push_back("forge.state");
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
    if (localization)
        result["localization"] = localization->descriptor;
    if (states)
        result["application_states"] = states->descriptor;
    ui_protocol::validate_snapshot(result);
    return result;
}
void UiRuntime::command(Scene& scene, const Json& request,
                        const std::function<void(const std::string&)>& control,
                        const std::function<void(double)>& time_scale) {
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
    auto conditions = scene.state_conditions();
    if (!d.enabled || !d.visible || d.document.id != asset || !conditions.active(e.id()))
        throw std::runtime_error("UI document no longer accepts input");
    if (AssetCatalog::open_project(project_).resolve(d.document).state != AssetState::Available)
        throw std::runtime_error("UI document asset is unavailable");
    const auto rows = scene.ui_document_rows();
    const auto localization = scene_localization(scene, rows);
    const auto states = scene_states(scene, rows);
    if (known->second.localization_scope != (localization ? localization->scope : std::string{}))
        throw std::runtime_error("UI localization binding changed; wait for a fresh snapshot");
    if (known->second.application_state_scope != (states ? states->scope : std::string{}))
        throw std::runtime_error("UI application state binding changed; wait for a fresh snapshot");
    const auto name = request.at("command").get<std::string>();
    if (name == "forge.state" && states) {
        if (actions_.contains(name) || !request.contains("value") ||
            !request.at("value").is_string())
            throw std::runtime_error(
                "forge.state requires an unambiguous domain:state:revision string");
        control_application_states(scene.world(),
                                   ui_protocol::application_state_request(
                                       request.at("value").get_ref<const std::string&>()));
        return;
    }
    if (name == "forge.locale" && localization) {
        if (actions_.contains(name) || !request.contains("value") ||
            !request.at("value").is_string())
            throw std::runtime_error("Localized forge.locale requires an unambiguous string value");
        scene.tune_runtime_property(localization->descriptor.at("entity").get<std::string>(),
                                    "forge.ui_localization", "locale", request.at("value"));
        return;
    }
    if (name == "TimeScale") {
        if (!time_scale || !request.contains("value") || !request.at("value").is_string())
            throw std::runtime_error("TimeScale requires a host clock and numeric string value");
        ui_protocol::validate_value(request.at("value"));
        const auto& text = request.at("value").get_ref<const std::string&>();
        double value = 0;
        const auto parsed = std::from_chars(text.data(), text.data() + text.size(), value);
        if (parsed.ec != std::errc{} || parsed.ptr != text.data() + text.size())
            throw std::runtime_error("TimeScale value must be a complete numeric string");
        // The existing clock owner admits the finite [0,4] policy atomically.
        time_scale(value);
        return;
    }
    if (request.contains("value")) {
        if (!value_actions_.contains(name) || !request.at("value").is_string())
            throw std::runtime_error("UI action does not accept a string value");
        ui_protocol::validate_value(request.at("value"));
    } else if (value_actions_.contains(name)) {
        throw std::runtime_error("UI action requires a string value");
    }
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
    value_actions_.clear();
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
