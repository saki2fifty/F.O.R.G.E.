#include <algorithm>
#include <cmath>
#include <forge/ecs_tools.hpp>
#include <set>
#include <stdexcept>
#include <thread>
namespace forge {
namespace {
std::string take(char* text) {
    if (!text)
        throw std::runtime_error("Flecs could not serialize this value");
    std::unique_ptr<char, decltype(ecs_os_api.free_)> owned(text, ecs_os_api.free_);
    return text;
}
} // namespace
struct EcsTools::Impl {
    WorldContext& context;
    flecs::world& world;
    std::thread::id owner = std::this_thread::get_id();
    std::vector<flecs::entity> definitions;
    std::vector<std::pair<ecs_entity_t, ecs_entity_t>> alerts;
    ecs_world_stats_t stats{};
    unsigned samples = 0;
    double stats_elapsed = 0;
    std::uint64_t stats_seconds = 0;
    std::set<ecs_entity_t> owned_metrics;
    ecs_http_server_t* rest = nullptr;
    ecs_http_server_t* listener = nullptr;
    unsigned port = 0;
    ecs_entity_t diagnostic_tick = 0;
    void stop_rest() {
        if (listener) {
            ecs_http_server_fini(listener);
            listener = nullptr;
        }
        if (rest) {
            ecs_rest_server_fini(rest);
            rest = nullptr;
        }
        port = 0;
    }
    static std::string url_encode(const char* text) {
        std::string result;
        constexpr char hex[] = "0123456789ABCDEF";
        for (const unsigned char c : std::string(text ? text : "")) {
            if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
                c == '-' || c == '_' || c == '.' || c == '~')
                result += char(c);
            else {
                result += '%';
                result += hex[c >> 4];
                result += hex[c & 15];
            }
        }
        return result;
    }
    static bool reply(const ecs_http_request_t* request, ecs_http_reply_t* reply,
                      void* context) noexcept {
        try {
            auto& self = *static_cast<Impl*>(context);
            self.check();
            const std::string path = request->path ? request->path : "";
            const bool readable = path.starts_with("entity/") || path == "query" ||
                                  path == "world" || path.starts_with("stats/") ||
                                  path == "components" || path == "queries" || path == "tables";
            if (request->method != EcsHttpGet || !readable) {
                reply->code = 403;
                ecs_strbuf_appendstr(&reply->body,
                                     "{\"error\":\"FORGE authoring-world REST permits inspection "
                                     "only. Use the authoring API for edits.\"}");
                return true;
            }
            std::string url = "/" + path;
            for (int32_t i = 0; i < request->param_count; ++i)
                url += std::string(i ? "&" : "?") + url_encode(request->params[i].key) + "=" +
                       url_encode(request->params[i].value);
            return ecs_http_server_request(self.rest, "GET", url.c_str(), nullptr, reply) == 0;
        } catch (...) {
            reply->code = 500;
            ecs_strbuf_appendstr(&reply->body, "{\"error\":\"ECS inspection failed\"}");
            return true;
        }
    }
    void check() const {
        if (std::this_thread::get_id() != owner)
            throw std::runtime_error("ECS inspection requires the world owner thread");
    }
    void run_diagnostic(ecs_entity_t id, float elapsed) {
        const auto* system = ecs_system_get(world, id);
        if (!system)
            throw std::runtime_error("Pinned diagnostic system is absent");
        const auto tick_source = system->tick_source;
        if (tick_source) {
            world.entity(diagnostic_tick).set<EcsTickSource>({true, elapsed});
            ecs_set_tick_source(world, id, diagnostic_tick);
        }
        ecs_run(world, id, elapsed, nullptr);
        if (tick_source)
            ecs_set_tick_source(world, id, tick_source);
    }
    explicit Impl(WorldContext& c) : context(c), world(c.world()) {
        try {
            std::size_t capacity = 2;
            for (const auto& type : context.schema().at("components"))
                capacity += type.at("fields").size();
            definitions.reserve(capacity);
            world.import<flecs::stats>();
            world.import<flecs::metrics>();
            world.import<flecs::alerts>();
            diagnostic_tick = world.entity().set<EcsTickSource>({false, 0}).id();
            definitions.push_back(world.entity(diagnostic_tick));
            for (const auto& component : context.schema().at("components")) {
                const auto type = world.lookup(component.at("id").get<std::string>().c_str());
                for (const auto& field : component.at("fields")) {
                    if (!field.contains("error_range") && !field.contains("warning_range"))
                        continue;
                    const auto member = type.lookup(field.at("id").get<std::string>().c_str());
                    ecs_alert_desc_t desc{};
                    desc.member = member;
                    desc.query.terms[0].id = type;
                    const auto label = component.at("display_name").get<std::string>() + " / " +
                                       field.at("display_name").get<std::string>();
                    desc.brief = label.c_str();
                    auto alert = ecs_alert_init(world, &desc);
                    if (!alert)
                        throw std::runtime_error("Flecs member alert registration failed");
                    definitions.push_back(world.entity(alert));
                    alerts.emplace_back(alert, member);
                }
            }
            // Actual authored-entity count, not a synthetic demonstration counter.
            ecs_metric_desc_t metric{};
            metric.id = world.id<PersistentEntityId>();
            metric.kind = EcsCounterId;
            metric.brief = "Authored entities in this world";
            auto count = ecs_metric_init(world, &metric);
            if (!count)
                throw std::runtime_error("Flecs entity-count metric registration failed");
            definitions.push_back(world.entity(count));
            owned_metrics.insert(count);
        } catch (...) {
            for (auto e : definitions)
                if (e.is_alive())
                    e.destruct();
            throw;
        }
    }
    ~Impl() {
        stop_rest();
        for (auto e : definitions)
            if (e.is_alive())
                e.destruct();
    }
};
EcsTools::EcsTools(WorldContext& context) : impl_(std::make_unique<Impl>(context)) {}
EcsTools::~EcsTools() = default;
Json EcsTools::query(const std::string& expression, unsigned offset, unsigned limit) {
    auto& p = *impl_;
    p.check();
    if (expression.empty() || expression.size() > 8192 || offset > unsigned(INT32_MAX) ||
        limit == 0 || limit > 1000)
        throw std::runtime_error("Query requires 1..8192 characters and a page of 1..1000 rows");
    ecs_query_desc_t desc{};
    desc.expr = expression.c_str();
    desc.cache_kind = EcsQueryCacheNone; // Exploratory queries are short-lived.
    desc.flags = EcsQueryMatchPrefab | EcsQueryMatchDisabled;
    auto* raw = ecs_query_init(p.world, &desc);
    if (!raw)
        throw std::runtime_error(
            "Invalid Flecs query expression; inspect Console for parser details");
    std::unique_ptr<ecs_query_t, decltype(&ecs_query_fini)> query(raw, ecs_query_fini);
    auto it = ecs_query_iter(p.world, raw);
    auto page = ecs_page_iter(&it, static_cast<int32_t>(offset), static_cast<int32_t>(limit));
    ecs_iter_to_json_desc_t json{};
    json.serialize_entity_ids = true;
    json.serialize_values = true;
    json.serialize_fields = true;
    json.serialize_full_paths = true;
    json.serialize_doc = true;
    json.serialize_field_info = true;
    return Json::parse(take(ecs_iter_to_json(&page, &json)));
}
Json EcsTools::entity(ecs_entity_t id) const {
    impl_->check();
    if (!ecs_is_alive(impl_->world, id))
        throw std::runtime_error("Entity ID is stale or absent in this world");
    return Json::parse(take(ecs_entity_to_json(impl_->world, id, nullptr)));
}
Json EcsTools::statistics() {
    auto& p = *impl_;
    p.check();
    ecs_world_stats_get(p.world, &p.stats);
    const auto t = p.stats.t;
    p.samples = std::min(p.samples + 1, unsigned(ECS_STAT_WINDOW));
    Json history = Json::array();
    for (unsigned i = 0; i < p.samples; ++i) {
        const int index = (t + ECS_STAT_WINDOW - int(p.samples) + 1 + int(i)) % ECS_STAT_WINDOW;
        history.push_back({{"entities", p.stats.entities.count.gauge.avg[index]},
                           {"tables", p.stats.tables.count.gauge.avg[index]}});
    }
    const auto& info = *ecs_get_world_info(p.world);
    return {{"entities", p.stats.entities.count.gauge.avg[t]},
            {"tables", p.stats.tables.count.gauge.avg[t]},
            {"empty_tables", p.stats.tables.empty_count.gauge.avg[t]},
            {"queries", p.stats.queries.query_count.gauge.avg[t]},
            {"observers", p.stats.queries.observer_count.gauge.avg[t]},
            {"systems", p.stats.queries.system_count.gauge.avg[t]},
            {"memory_bytes", ecs_memory_get(p.world)},
            {"frame_count", info.frame_count_total},
            {"world_delta", info.delta_time},
            {"stages", ecs_get_stage_count(p.world)},
            {"history", history}};
}
std::string EcsTools::export_world() const {
    impl_->check();
    return take(ecs_world_to_json(impl_->world, nullptr));
}
void EcsTools::sample(float elapsed) {
    auto& p = *impl_;
    p.check();
    if (!std::isfinite(elapsed) || elapsed <= 0 || elapsed > 1)
        throw std::runtime_error("ECS diagnostic sample requires elapsed time in (0,1]");
    // Only imported diagnostic systems run. Never progress the authoring world,
    // gameplay systems, fixed clock, or native timer state from editor wall time.
    for (const char* path :
         {"flecs.metrics.ClearMetricInstance", "flecs.metrics.UpdateGaugeMemberInstance",
          "flecs.metrics.UpdateCounterMemberInstance",
          "flecs.metrics.UpdateCounterIncrementMemberInstance",
          "flecs.metrics.UpdateGaugeIdInstance", "flecs.metrics.UpdateCounterIdInstance",
          "flecs.metrics.UpdateGaugeOneOfInstance", "flecs.metrics.UpdateCounterOneOfInstance",
          "flecs.metrics.UpdateCountIds", "flecs.metrics.UpdateCountTargets",
          "flecs.alerts.MonitorAlerts", "flecs.alerts.MonitorAlertInstances"}) {
        const auto id = ecs_lookup(p.world, path);
        if (!id)
            throw std::runtime_error("Pinned Flecs diagnostic system is missing: " +
                                     std::string(path));
        p.run_diagnostic(id, elapsed);
    }
    // Sample/reduce through the native Stats systems, including the periods REST
    // reads. Their scheduling is diagnostic time; no gameplay timers are advanced.
    auto monitor = [&](const char* name, float delta) {
        // An authoring/inspection world does not execute a gameplay pipeline.
        // Pinned 4.1.6 cannot safely reduce an empty pipeline's systems vector.
        // Leave pipeline sampling to actual runtime progress; do not manufacture
        // pipeline history by manually running its reduction systems here.
        for (const auto type : {ecs_id(EcsWorldStats), ecs_id(EcsSystemStats)})
            p.run_diagnostic(ecs_lookup_child(p.world, type, name), delta);
    };
    monitor("Monitor1s", elapsed);
    p.stats_elapsed += elapsed;
    while (p.stats_elapsed >= 1.0) {
        p.stats_elapsed -= 1.0;
        monitor("Monitor1m", 1.f);
        if (++p.stats_seconds % 60 == 0) {
            // All three use rate(60, Monitor1m) in the exact pinned source.
            monitor("Monitor1h", 60.f);
            monitor("Monitor1d", 60.f);
            monitor("Monitor1w", 60.f);
        }
    }
}
Json EcsTools::alerts() const {
    auto& p = *impl_;
    p.check();
    Json result = Json::array();
    auto it = ecs_each_id(p.world, ecs_id(EcsAlertsActive));
    while (ecs_each_next(&it)) {
        for (int32_t i = 0; i < it.count; ++i) {
            const auto entity = it.entities[i];
            const auto* active = ecs_get(p.world, entity, EcsAlertsActive);
            auto entries = ecs_map_iter(&active->alerts);
            while (ecs_map_next(&entries)) {
                const auto definition = ecs_map_key(&entries);
                const auto instance = ecs_map_value(&entries);
                ecs_entity_t member = 0;
                for (const auto& pair : p.alerts)
                    if (pair.first == definition) {
                        member = pair.second;
                        break;
                    }
                const auto* alert = ecs_get(p.world, instance, EcsAlertInstance);
                const auto severity = ecs_get_target(p.world, instance, ecs_id(EcsAlert), 0);
                const auto ref = p.context.reference(entity);
                const char* message = alert && alert->message
                                          ? alert->message
                                          : ecs_doc_get_brief(p.world, definition);
                if (!message)
                    message = "Native Flecs alert";
                result.push_back({{"key", "flecs/" + std::to_string(instance)},
                                  {"severity", severity == EcsAlertInfo      ? "Info"
                                               : severity == EcsAlertWarning ? "Warning"
                                                                             : "Error"},
                                  {"text", message},
                                  {"entity", ref ? ref->entity.str() : std::string{}},
                                  {"property", member ? ecs_get_name(p.world, member) : ""}});
            }
        }
    }
    return result;
}
ecs_entity_t EcsTools::create_metric(ecs_entity_t source, const std::string& kind, bool member) {
    auto& p = *impl_;
    p.check();
    if (p.owned_metrics.size() >= 64 || !ecs_is_alive(p.world, source))
        throw std::runtime_error("Metric source is absent or the 64-metric limit was reached");
    ecs_metric_desc_t desc{};
    if (kind == "gauge")
        desc.kind = EcsGauge;
    else if (kind == "counter")
        desc.kind = EcsCounter;
    else if (kind == "increment")
        desc.kind = EcsCounterIncrement;
    else if (kind == "count")
        desc.kind = EcsCounterId;
    else
        throw std::runtime_error("Choose gauge, counter, increment or count");
    if (member) {
        const auto* m = ecs_get(p.world, source, EcsMember);
        const auto* primitive = m ? ecs_get(p.world, m->type, EcsPrimitive) : nullptr;
        if (!primitive || primitive->kind < EcsU8 || primitive->kind > EcsF64 ||
            desc.kind == EcsCounterId)
            throw std::runtime_error(
                "This metric requires a numeric reflected member and a compatible kind");
        desc.member = source;
    } else {
        if (!ecs_has(p.world, source, EcsComponent) || desc.kind == EcsCounterIncrement)
            throw std::runtime_error(
                "Choose a component ID; increment metrics require a numeric member");
        desc.id = source;
    }
    const char* name = ecs_doc_get_name(p.world, source);
    const auto label = kind + " / " + (name ? name : "component");
    desc.brief = label.c_str();
    // Reserve before the native registration so ownership cannot be lost to growth.
    p.definitions.reserve(p.definitions.size() + 1);
    const auto metric = ecs_metric_init(p.world, &desc);
    if (!metric)
        throw std::runtime_error("Native metric registration rejected this source");
    try {
        p.owned_metrics.insert(metric);
    } catch (...) {
        ecs_delete(p.world, metric);
        throw;
    }
    p.definitions.push_back(p.world.entity(metric));
    return metric;
}
void EcsTools::remove_metric(ecs_entity_t metric) {
    auto& p = *impl_;
    p.check();
    if (!p.owned_metrics.erase(metric))
        throw std::runtime_error("Metric is not owned by this tool");
    ecs_delete(p.world, metric);
    std::erase_if(p.definitions, [&](flecs::entity e) { return e.id() == metric; });
}
Json EcsTools::metrics() const {
    auto& p = *impl_;
    p.check();
    Json result = Json::array();
    auto it = ecs_each_id(p.world, ecs_id(EcsMetricValue));
    while (ecs_each_next(&it))
        for (int32_t i = 0; i < it.count; ++i) {
            const auto id = it.entities[i];
            const auto* value = ecs_get(p.world, id, EcsMetricValue);
            const auto* source = ecs_get(p.world, id, EcsMetricSource);
            const auto parent = ecs_get_target(p.world, id, EcsChildOf, 0);
            const auto definition = parent ? parent : id;
            const auto* label = ecs_doc_get_brief(p.world, definition);
            result.push_back({{"id", id},
                              {"metric", definition},
                              {"value", value->value},
                              {"source", source ? source->entity : 0},
                              {"label", label ? label : "Native metric"},
                              {"removable", p.owned_metrics.contains(definition)}});
        }
    return result;
}
void EcsTools::start_rest(unsigned port) {
    auto& p = *impl_;
    p.check();
    if (port < 1024 || port > 65535)
        throw std::runtime_error("Choose a local port from 1024 to 65535");
    if (p.listener)
        throw std::runtime_error("Stop the current REST listener before changing its port");
    ecs_http_server_desc_t native{};
    native.ipaddr = "127.0.0.1";
    p.rest = ecs_rest_server_init(p.world, &native); // Never started; native dispatcher only.
    if (!p.rest)
        throw std::runtime_error("Cannot initialize native Flecs REST");
    ecs_http_server_desc_t server{};
    server.ipaddr = "127.0.0.1";
    server.port = static_cast<uint16_t>(port);
    server.callback = Impl::reply;
    server.ctx = &p;
    p.listener = ecs_http_server_init(&server);
    if (!p.listener || ecs_http_server_start(p.listener)) {
        p.stop_rest();
        throw std::runtime_error("Cannot start loopback REST; the port may already be in use");
    }
    p.port = port;
}
void EcsTools::stop_rest() {
    impl_->check();
    impl_->stop_rest();
}
void EcsTools::poll_rest(float elapsed) {
    impl_->check();
    if (impl_->listener)
        ecs_http_server_dequeue(impl_->listener, elapsed);
}
unsigned EcsTools::rest_port() const { return impl_->port; }
Json EcsTools::rest_request(const std::string& method, const std::string& path) {
    impl_->check();
    if (!impl_->listener)
        throw std::runtime_error("REST is stopped");
    ecs_http_reply_t reply{200, {}, "OK", "application/json", {}};
    const int status =
        ecs_http_server_request(impl_->listener, method.c_str(), path.c_str(), nullptr, &reply);
    const auto text = take(ecs_strbuf_get(&reply.body));
    ecs_strbuf_reset(&reply.headers);
    return {{"transport", status}, {"status", reply.code}, {"body", text}};
}
} // namespace forge
