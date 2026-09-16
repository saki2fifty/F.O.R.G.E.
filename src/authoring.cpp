#include "scene_draft.hpp"
#include <algorithm>
#include <atomic>
#include <chrono>
#include <forge/authoring.hpp>
#include <forge/geometry.hpp>
#include <set>
namespace forge {
namespace {
struct CommandError : std::runtime_error {
    std::string code;
    CommandError(std::string c, const std::string& message)
        : std::runtime_error(message), code(std::move(c)) {}
};
Json object(Json properties = Json::object(), Json required = Json::array()) {
    return {{"type", "object"},
            {"properties", properties},
            {"required", required},
            {"additionalProperties", false}};
}
Json number(double low, double high) {
    return {{"type", "number"}, {"minimum", low}, {"maximum", high}};
}
Json xyz() {
    return object({{"x", number(-1e6, 1e6)}, {"y", number(-1e6, 1e6)}, {"z", number(-1e6, 1e6)}},
                  {"x", "y", "z"});
}
Json text_type() { return {{"type", "string"}, {"maxLength", 1024}}; }
void validate_value(const Json& value, const Json& schema, const std::string& path) {
    const auto type = schema.at("type").get<std::string>();
    bool valid = type == "object"    ? value.is_object()
                 : type == "string"  ? value.is_string()
                 : type == "integer" ? value.is_number_integer()
                                     : value.is_number();
    if (!valid)
        throw CommandError("invalid_arguments", path + ": expected " + type);
    if (type == "object") {
        for (const auto& field : schema.at("required"))
            if (!value.contains(field.get<std::string>()))
                throw CommandError("invalid_arguments",
                                   path + ": missing " + field.get<std::string>());
        for (const auto& [key, item] : value.items()) {
            if (!schema.at("properties").contains(key))
                throw CommandError("invalid_arguments", path + ": unknown field " + key);
            validate_value(item, schema.at("properties").at(key), path + "." + key);
        }
    } else if (type == "string") {
        const auto& text = value.get_ref<const std::string&>();
        const auto length = std::count_if(text.begin(), text.end(),
                                          [](unsigned char c) { return (c & 0xc0) != 0x80; });
        if (std::size_t(length) > schema.value("maxLength", 1024u))
            throw CommandError("invalid_arguments", path + ": text too long");
    } else {
        const auto n = value.get<double>();
        if (!std::isfinite(n) || n < schema.value("minimum", -1e100) ||
            n > schema.value("maximum", 1e100))
            throw CommandError("invalid_arguments", path + ": value outside supported range");
    }
}
Json& entity(Json& doc, const std::string& id) {
    for (auto& e : doc["entities"])
        if (e.at("id") == id)
            return e;
    throw CommandError("not_found", "Entity not found: " + id);
}
template <class View> Json effective(const View& scene, const std::string& id) {
    auto doc = scene.effective_document();
    return entity(doc, id);
}
std::string free_id(const Json& doc) {
    std::set<std::string> occupied;
    for (const auto& e : doc.at("entities"))
        occupied.insert(e.at("id").get<std::string>());
    unsigned n = 1;
    while (occupied.contains("entity-" + std::to_string(n)))
        ++n;
    return "entity-" + std::to_string(n);
}
Json property_schema(const detail::SceneDraft& scene, const std::string& component,
                     const std::string& field) {
    const auto reflected = scene.schema();
    for (const auto& c : reflected.at("components")) {
        if (c.at("id") != component)
            continue;
        for (const auto& f : c.at("fields"))
            if (f.at("id") == field)
                return f;
    }
    throw CommandError("unsupported_property",
                       "Unsupported reflected property: " + component + "." + field);
}
void set_fields(detail::SceneDraft& scene, const std::string& id, const std::string& component,
                const Json& values) {
    auto doc = scene.document();
    auto& c = entity(doc, id)["components"];
    if (!c.contains(component)) {
        const auto e = effective(scene, id);
        if (e.at("components").contains(component))
            c[component] = e.at("components").at(component);
        else {
            const auto schema = scene.schema();
            for (const auto& item : schema.at("components"))
                if (item.at("id") == component) {
                    c[component] = Json::object();
                    for (const auto& f : item.at("fields"))
                        c[component][f.at("id").get<std::string>()] = f.at("default");
                }
        }
    }
    for (const auto& [field, value] : values.items()) {
        const auto schema = property_schema(scene, component, field);
        if (!value.is_number() || (schema.at("type") == "uint32" && !value.is_number_integer()))
            throw CommandError("invalid_arguments", "Property requires its declared numeric type");
        const auto n = value.get<double>();
        if (!std::isfinite(n) ||
            (schema.contains("minimum") && n < schema.at("minimum").get<double>()) ||
            (schema.contains("maximum") && n > schema.at("maximum").get<double>()))
            throw CommandError("invalid_arguments", "Property outside declared range");
        c[component][field] = value;
    }
    scene.edit(doc);
}
Json execute(detail::SceneDraft& scene, const std::string& op, const Json& a) {
    const auto id = a.value("entity", std::string{});
    if (!id.empty()) {
        auto doc = scene.document();
        (void)entity(doc, id);
    }
    Json result = {{"operation", op}, {"selected", id}};
    if (op == "entity.create") {
        auto doc = scene.document();
        const auto created = free_id(doc);
        const unsigned kind = a.value("kind", 0u);
        const auto p = a.value("position", Json{{"x", 0}, {"y", kind == 3 ? 0 : 1}, {"z", 0}});
        doc["entities"].push_back(
            {{"id", created},
             {"name",
              a.value("name", std::string(primitive_names[kind]) + " " + created.substr(7))},
             {"components",
              {{"forge.position", p},
               {"forge.rotation", {{"x", 0}, {"y", 0}, {"z", 0}}},
               {"forge.scale", {{"x", kind == 3 ? 4 : 1}, {"y", 1}, {"z", kind == 3 ? 4 : 1}}},
               {"forge.tint", {{"r", 0.2f}, {"g", 0.6f}, {"b", 0.7f}}},
               {"forge.primitive", {{"kind", kind}}}}}});
        const auto name = doc["entities"].back().at("name").get<std::string>();
        if (name.find_first_not_of(" \t\r\n") == std::string::npos)
            throw CommandError("invalid_arguments", "Entity name must not be blank");
        scene.edit(doc);
        result["selected"] = created;
    } else if (op == "entity.rename")
        scene.rename_entity(id, a.at("name"));
    else if (op == "entity.reparent")
        scene.reparent_entity(id, a.at("parent"));
    else if (op == "entity.duplicate")
        result["selected"] = scene.duplicate_subtree(id);
    else if (op == "entity.delete") {
        scene.delete_subtree(id);
        result["selected"] = "";
    } else if (op == "property.set")
        set_fields(scene, id, a.at("component"),
                   {{a.at("field").get<std::string>(), a.at("value")}});
    else if (op == "transform.position" || op == "transform.rotation" || op == "transform.scale")
        set_fields(scene, id, "forge." + op.substr(10), a.at("value"));
    else if (op == "appearance.color")
        set_fields(scene, id, "forge.tint", a.at("value"));
    else if (op == "appearance.shape")
        set_fields(scene, id, "forge.primitive", {{"kind", a.at("kind")}});
    else if (op == "transform.reset") {
        set_fields(scene, id, "forge.position", {{"x", 0}, {"y", 0}, {"z", 0}});
        set_fields(scene, id, "forge.rotation", {{"x", 0}, {"y", 0}, {"z", 0}});
        set_fields(scene, id, "forge.scale", {{"x", 1}, {"y", 1}, {"z", 1}});
    } else if (op == "transform.ground" || op == "transform.snap") {
        const auto e = effective(scene, id);
        if (!e.at("components").contains("forge.position"))
            throw CommandError("unavailable", "Entity has no position");
        auto p = read_xyz(e.at("components"), "forge.position", {});
        if (op == "transform.ground")
            p[1] -= object_bounds(e).first[1];
        else
            for (auto& coordinate : p)
                coordinate = float(std::round(coordinate / a.at("step").get<double>()) *
                                   a.at("step").get<double>());
        set_fields(scene, id, "forge.position", {{"x", p[0]}, {"y", p[1]}, {"z", p[2]}});
    } else if (op == "transform.copy_from") {
        const auto source = effective(scene, a.at("source"));
        for (const char* c : {"forge.position", "forge.rotation", "forge.scale"}) {
            const auto p = read_xyz(source.at("components"), c,
                                    std::string(c) == "forge.scale" ? Float3{1, 1, 1} : Float3{});
            set_fields(scene, id, c, {{"x", p[0]}, {"y", p[1]}, {"z", p[2]}});
        }
    } else if (op == "component.revert") {
        const auto component = a.at("component").get<std::string>();
        const auto schema = scene.schema();
        bool known = false;
        for (const auto& c : schema.at("components"))
            known |= c.at("id") == component;
        if (!known)
            throw CommandError("unsupported_property",
                               "Only supported built-in overrides can be removed");
        auto doc = scene.document();
        entity(doc, id)["components"].erase(component);
        scene.edit(doc);
    } else
        throw CommandError("unknown_operation", "Unknown authoring operation: " + op);
    return result;
}
} // namespace
Json authoring_commands() {
    Json commands = Json::array();
    auto add = [&](const char* id, const char* label, const char* help, Json properties,
                   Json required) {
        commands.push_back({{"id", id},
                            {"label", label},
                            {"description", help},
                            {"capability", "scene.edit"},
                            {"input_schema", object(properties, required)}});
    };
    const auto entity_arg = Json{{"entity", text_type()}};
    add("entity.create", "Create primitive", "Create a cube, sphere, cylinder or plane.",
        {{"kind", {{"type", "integer"}, {"minimum", 0}, {"maximum", 3}}},
         {"name", text_type()},
         {"position", xyz()}},
        Json::array());
    auto named = entity_arg;
    named["name"] = text_type();
    add("entity.rename", "Rename entity", "Rename without changing stable identity.", named,
        {"entity", "name"});
    auto parent = entity_arg;
    parent["parent"] = text_type();
    add("entity.reparent", "Move to parent",
        "Change organization; preserve world-space transform. Empty parent means root.", parent,
        {"entity", "parent"});
    add("entity.duplicate", "Duplicate subtree",
        "Copy the selected entity and descendants, remapping internal references.", entity_arg,
        {"entity"});
    add("entity.delete", "Delete subtree",
        "Delete the entity and descendants; reject external prefab users.", entity_arg, {"entity"});
    for (const char* axis : {"position", "rotation", "scale"}) {
        const auto op = std::string("transform.") + axis;
        auto args = entity_arg;
        args["value"] = xyz();
        add(op.c_str(), ("Set " + std::string(axis)).c_str(),
            "Set supported XYZ values; reflected property constraints apply.", args,
            {"entity", "value"});
    }
    auto color = entity_arg;
    color["value"] =
        object({{"r", number(0, 1)}, {"g", number(0, 1)}, {"b", number(0, 1)}}, {"r", "g", "b"});
    add("appearance.color", "Set color", "Set opaque RGB blockout tint.", color,
        {"entity", "value"});
    auto shape = entity_arg;
    shape["kind"] = {{"type", "integer"}, {"minimum", 0}, {"maximum", 3}};
    add("appearance.shape", "Set shape", "Change primitive while preserving transform and color.",
        shape, {"entity", "kind"});
    add("transform.reset", "Reset transform", "Zero position/rotation and unit scale.", entity_arg,
        {"entity"});
    add("transform.ground", "Place on ground", "Place transformed mesh bottom at world Y=0.",
        entity_arg, {"entity"});
    auto snap = entity_arg;
    snap["step"] = number(0.001, 10000);
    add("transform.snap", "Snap position", "Round each world coordinate to the supplied step.",
        snap, {"entity", "step"});
    auto copy = entity_arg;
    copy["source"] = text_type();
    add("transform.copy_from", "Copy transform from entity",
        "Copy effective position, rotation and scale from a source entity.", copy,
        {"entity", "source"});
    auto field = entity_arg;
    field["component"] = text_type();
    field["field"] = text_type();
    field["value"] = number(-1e38, 1e38);
    add("property.set", "Set reflected property",
        "Validate against the supported reflected property schema.", field,
        {"entity", "component", "field", "value"});
    auto component = entity_arg;
    component["component"] = text_type();
    add("component.revert", "Remove component override",
        "Remove an owned built-in component; inherited values may become visible.", component,
        {"entity", "component"});
    return commands;
}
Json apply_authoring(Scene& scene, const Json& commands, std::uint64_t revision) {
    if (revision != scene.revision())
        throw CommandError("stale_revision", "Scene changed; read current state before retrying");
    if (!commands.is_array() || commands.empty() || commands.size() > 128)
        throw CommandError("invalid_arguments", "Expected 1 to 128 commands");
    detail::SceneDraft candidate(scene);
    const auto catalog = authoring_commands();
    Json results = Json::array();
    for (const auto& command : commands) {
        if (!command.is_object() || !command.contains("operation") ||
            !command.at("operation").is_string() || command.size() > 2)
            throw CommandError("invalid_arguments", "Expected operation and arguments");
        for (const auto& [key, value] : command.items()) {
            (void)value;
            if (key != "operation" && key != "arguments")
                throw CommandError("invalid_arguments", "Unknown command field: " + key);
        }
        const auto op = command.at("operation").get<std::string>();
        bool known = false;
        for (const auto& descriptor : catalog)
            if (descriptor.at("id") == op) {
                const auto args = command.value("arguments", Json::object());
                validate_value(args, descriptor.at("input_schema"), op);
                results.push_back(execute(candidate, op, args));
                if (candidate.entity_count() > 10000 ||
                    candidate.document().dump().size() > 8 * 1024 * 1024)
                    throw CommandError("limit_exceeded",
                                       "Candidate exceeds 10000 entities or 8 MiB serialized data");
                known = true;
                break;
            }
        if (!known)
            throw CommandError("unknown_operation", "Unknown authoring operation: " + op);
    }
    const auto next = candidate.document();
    if (next.at("entities").size() > 10000)
        throw CommandError("limit_exceeded", "Authoring commands support at most 10000 entities");
    const auto before = scene.revision();
    scene.edit(next);
    return {{"changed", before != scene.revision()},
            {"revision", scene.revision()},
            {"results", results}};
}
Json authoring_command(Scene& scene, const std::string& operation, const Json& arguments) {
    return apply_authoring(scene,
                           Json::array({{{"operation", operation}, {"arguments", arguments}}}),
                           scene.revision())
        .at("results")
        .at(0);
}
Json scene_diagnostics(const Scene& scene) {
    const auto doc = scene.document();
    const auto view = scene.effective_document();
    Json items = Json::array();
    std::map<std::string, unsigned> names;
    std::set<std::string> supported;
    const auto schema = scene.schema();
    for (const auto& c : schema.at("components"))
        supported.insert(c.at("id"));
    std::size_t visible = 0, prefabs = 0, components = 0;
    for (const auto& e : doc.at("entities"))
        ++names[e.at("name").get<std::string>()];
    auto add = [&](const Json& e, const char* code, const char* message,
                   const std::string& component = "") {
        items.push_back({{"severity", "info"},
                         {"code", code},
                         {"entity", e.at("id")},
                         {"component", component},
                         {"message", message}});
    };
    for (const auto& e : view.at("entities")) {
        const bool prefab = e.value("prefab", false);
        prefabs += prefab;
        if (!prefab && e.at("components").contains("forge.position"))
            ++visible;
        if (names[e.at("name").get<std::string>()] > 1)
            add(e, "duplicate_name", "Display name is shared; stable IDs remain distinct.");
        if (!prefab && !e.at("components").contains("forge.position"))
            add(e, "no_position", "Entity has no effective Position and is not drawn.");
    }
    for (const auto& e : doc.at("entities"))
        for (const auto& [name, value] : e.at("components").items()) {
            (void)value;
            ++components;
            if (!supported.contains(name))
                add(e, "unknown_component",
                    "Data is preserved, but this component has no registered runtime integration.",
                    name);
        }
    return {
        {"revision", scene.revision()}, {"entities", scene.entity_count()}, {"visible", visible},
        {"prefabs", prefabs},           {"owned_components", components},   {"items", items}};
}
AuthoringSession::AuthoringSession(Scene& scene)
    : scene_(scene), owner_(std::this_thread::get_id()) {
    static std::atomic<unsigned long long> next{0};
    session_ = std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + "-" +
               std::to_string(++next);
}
Json AuthoringSession::target() const {
    return {{"kind", "scene"}, {"id", "session-scene"}, {"session", session_}};
}
Json AuthoringSession::handle(const Json& request) {
    Json response = {{"api", 1}, {"ok", false}};
    try {
        if (std::this_thread::get_id() != owner_)
            throw CommandError("wrong_thread", "Dispatch on the session owning thread");
        if (!request.is_object() || request.value("api", 0) != 1)
            throw CommandError("unsupported_version", "Expected authoring API 1");
        const auto method = request.at("method").get<std::string>();
        if (method == "discover")
            response["result"] = {
                {"target", target()},
                {"revision", scene_.revision()},
                {"commands", authoring_commands()},
                {"schema", scene_.schema()},
                {"methods",
                 {"discover", "scene.read", "entity.query", "scene.diagnostics", "scene.replace",
                  "scene.apply", "history.undo", "history.redo"}},
                {"capabilities", {"scene.read", "scene.edit", "history"}},
                {"persistence", "memory-only"},
                {"limits", {{"batch_commands", 128}, {"entities", 10000}, {"query_page", 256}}}};
        else {
            if (request.at("target") != target())
                throw CommandError("wrong_target",
                                   "Target does not identify this session document");
            if (method == "scene.read")
                response["result"] = scene_.document();
            else if (method == "scene.diagnostics")
                response["result"] = scene_diagnostics(scene_);
            else if (method == "entity.query") {
                const auto limit = request.value("limit", 128);
                const auto offset = request.value("offset", 0);
                if (limit < 1 || limit > 256 || offset < 0)
                    throw CommandError("invalid_arguments",
                                       "Query limit must be 1..256 and offset nonnegative");
                const auto filter = request.value("text", std::string{});
                const auto component = request.value("component", std::string{});
                const auto doc = scene_.effective_document();
                Json hits = Json::array();
                std::size_t matched = 0;
                for (const auto& e : doc.at("entities"))
                    if ((filter.empty() ||
                         e.at("name").get<std::string>().find(filter) != std::string::npos ||
                         e.at("id").get<std::string>().find(filter) != std::string::npos) &&
                        (component.empty() || e.at("components").contains(component))) {
                        if (matched >= std::size_t(offset) && hits.size() < std::size_t(limit))
                            hits.push_back(e);
                        ++matched;
                    }
                response["result"] = {{"entities", hits}, {"total", matched}, {"offset", offset}};
            } else {
                if (!request.at("expected_revision").is_number_unsigned() &&
                    !(request.at("expected_revision").is_number_integer() &&
                      request.at("expected_revision").get<std::int64_t>() >= 0))
                    throw CommandError("invalid_arguments", "Expected an unsigned revision");
                if (request.at("expected_revision").get<std::uint64_t>() != scene_.revision())
                    throw CommandError("stale_revision",
                                       "Scene changed; read current state before retrying");
                if (method == "scene.apply")
                    response["result"] =
                        apply_authoring(scene_, request.at("commands"), scene_.revision());
                else if (method == "scene.replace") {
                    const auto& doc = request.at("document");
                    if (doc.at("entities").size() > 10000)
                        throw CommandError("limit_exceeded", "At most 10000 entities supported");
                    scene_.edit(doc);
                    response["result"] = {{"revision", scene_.revision()}};
                } else if (method == "history.undo")
                    response["result"] = {{"changed", authoring_history(scene_, false)}};
                else if (method == "history.redo")
                    response["result"] = {{"changed", authoring_history(scene_, true)}};
                else
                    throw CommandError("unknown_method", "Unsupported authoring method: " + method);
            }
        }
        response["ok"] = true;
        response["revision"] = scene_.revision();
    } catch (const CommandError& e) {
        response["error"] = {{"code", e.code}, {"message", e.what()}};
    } catch (const std::exception& e) {
        response["error"] = {{"code", "invalid_request"}, {"message", e.what()}};
    }
    return response;
}
} // namespace forge
