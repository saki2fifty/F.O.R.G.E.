#include "scene_draft.hpp"
#include "spatial_document.hpp"
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
    return object(
        {{"x", number(-1e12, 1e12)}, {"y", number(-1e12, 1e12)}, {"z", number(-1e12, 1e12)}},
        {"x", "y", "z"});
}
Json text_type() { return {{"type", "string"}, {"maxLength", 1024}}; }
void validate_value(const Json& value, const Json& schema, const std::string& path) {
    if (schema.contains("anyOf")) {
        for (const auto& candidate : schema.at("anyOf")) {
            try {
                validate_value(value, candidate, path);
                return;
            } catch (const CommandError&) {
            }
        }
        throw CommandError("invalid_arguments", path + ": unsupported property value type");
    }
    const auto type = schema.at("type").get<std::string>();
    bool valid = type == "object"    ? value.is_object()
                 : type == "string"  ? value.is_string()
                 : type == "boolean" ? value.is_boolean()
                 : type == "null"    ? value.is_null()
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
    } else if (value.is_number()) {
        const auto n = value.get<double>();
        if (!std::isfinite(n) || n < schema.value("minimum", -1e100) ||
            n > schema.value("maximum", 1e100))
            throw CommandError("invalid_arguments", path + ": value outside supported range");
    }
}
Json& entity(Json& doc, const std::string& id) {
    for (auto& e : doc["entities"])
        if (e.at("id") == resolve_legacy_id(doc, id))
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
    std::string id;
    do {
        id = EntityId::generate().str();
    } while (occupied.contains(id));
    return id;
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
    auto& e = entity(doc, id);
    const bool legacy = component == "forge.position" || component == "forge.rotation" ||
                        component == "forge.scale";
    const auto view = effective(scene, id);
    std::string canonical = component == "forge.position"   ? "forge.local_translation"
                            : component == "forge.rotation" ? "forge.local_rotation"
                            : component == "forge.scale"    ? "forge.local_scale"
                                                            : component;
    const bool property_intent = (e.contains("prefab_instance") || e.contains("prefab_member")) &&
                                 (!canonical.starts_with("forge.local_")) &&
                                 !e["components"].contains(canonical);
    if (legacy) {
        auto merged = view.at("components").value(component, Json::object());
        if (merged.empty())
            merged = {{"x", component == "forge.scale" ? 1 : 0},
                      {"y", component == "forge.scale" ? 1 : 0},
                      {"z", component == "forge.scale" ? 1 : 0}};
        for (const auto& [field, value] : values.items()) {
            if (field != "x" && field != "y" && field != "z")
                throw CommandError("unsupported_property", "Unknown transform display field");
            if (!value.is_number() || !std::isfinite(value.get<double>()))
                throw CommandError("invalid_arguments", "Invalid transform value");
            if (component == "forge.rotation" && std::abs(value.get<double>()) > 360000)
                throw CommandError("invalid_arguments", "Euler degrees out of range");
            merged[field] = value;
        }
        detail::write_channel(e, component.c_str(), merged);
    } else {
        auto& c = e["components"];
        if (!c.contains(canonical)) {
            if (view.at("components").contains(canonical))
                c[canonical] = view.at("components").at(canonical);
            else {
                const auto schema = scene.schema();
                for (const auto& item : schema.at("components"))
                    if (item.at("id") == canonical)
                        for (const auto& f : item.at("fields"))
                            c[canonical][f.at("id").get<std::string>()] = f.at("default");
            }
        }
        for (const auto& [field, value] : values.items()) {
            auto schema = property_schema(scene, canonical, field);
            if (schema.at("type") == "asset_ref") {
                if (!value.is_null())
                    (void)value.get<AssetId>();
            } else if (schema.at("type") == "bool") {
                if (!value.is_boolean())
                    throw CommandError("invalid_arguments", "Boolean required");
            } else {
                if (!value.is_number() ||
                    (schema.at("type") == "uint32" && !value.is_number_integer()))
                    throw CommandError("invalid_arguments",
                                       "Property requires its declared numeric type");
                double n = value.get<double>();
                if (!std::isfinite(n) ||
                    (schema.contains("minimum") && n < schema.at("minimum").get<double>()) ||
                    (schema.contains("maximum") && n > schema.at("maximum").get<double>()))
                    throw CommandError("invalid_arguments", "Property outside declared range");
            }
            if (property_intent)
                e["property_overrides"][canonical][field] = value;
            c[canonical][field] = value;
        }
        if (canonical == "forge.local_rotation") {
            const auto& q = c.at(canonical);
            c[canonical].update(
                detail::encode(normalized({q.at("x"), q.at("y"), q.at("z"), q.at("w")})));
        }
    }
    if (property_intent)
        e["components"].erase(canonical);
    scene.edit(doc);
}

Json execute(detail::SceneDraft& scene, const std::string& op, const Json& a) {
    const auto id = resolve_legacy_id(scene.document(), a.value("entity", std::string{}));
    if (!id.empty()) {
        auto doc = scene.document();
        (void)entity(doc, id);
    }
    Json result = {{"operation", op}, {"selected", id}};
    if (op == "prefab.instantiate")
        result["selected"] = scene.instantiate_prefab(a.at("asset").get<AssetId>());
    else if (op == "prefab.revert_name")
        scene.revert_prefab_name(id);
    else if (op == "entity.create") {
        auto doc = scene.document();
        const auto created = free_id(doc);
        const unsigned kind = a.value("kind", 0u);
        const auto p = a.value("position", Json{{"x", 0}, {"y", kind == 3 ? 0 : 1}, {"z", 0}});
        doc["entities"].push_back(
            {{"id", created},
             {"name", a.value("name", std::string(primitive_names[kind]) + " " +
                                          std::to_string(doc["entities"].size() + 1))},
             {"components",
              {{"forge.position", p},
               {"forge.rotation", {{"x", 0}, {"y", 0}, {"z", 0}}},
               {"forge.scale", {{"x", kind == 3 ? 4 : 1}, {"y", 1}, {"z", kind == 3 ? 4 : 1}}},
               {"forge.tint", {{"r", 0.2f}, {"g", 0.6f}, {"b", 0.7f}}},
               {"forge.primitive", {{"kind", kind}}}}}});
        auto& created_row = doc["entities"].back();
        for (auto channel : {"forge.position", "forge.rotation", "forge.scale"}) {
            detail::write_channel(created_row, channel, created_row["components"].at(channel));
            created_row["components"].erase(channel);
        }
        created_row["spatial"] = {{"mode", "follow_structure"}};
        const auto name = doc["entities"].back().at("name").get<std::string>();
        if (name.find_first_not_of(" \t\r\n") == std::string::npos)
            throw CommandError("invalid_arguments", "Entity name must not be blank");
        scene.edit(doc);
        result["selected"] = created;
    } else if (op == "entity.rename")
        scene.rename_entity(id, a.at("name"));
    else if (op == "entity.reparent")
        scene.reparent_entity(id, a.at("parent"),
                              a.value("mode", std::string("preserve_world")) == "keep_local"
                                  ? ReparentMode::KeepLocal
                                  : ReparentMode::PreserveWorld);
    else if (op == "transform.binding") {
        auto doc = scene.document();
        detail::rebind(doc, scene.effective_document(), id, a.at("spatial"), nullptr,
                       a.value("mode", std::string("preserve_world")) == "keep_local"
                           ? ReparentMode::KeepLocal
                           : ReparentMode::PreserveWorld);
        scene.edit(doc);
    } else if (op == "transform.world_translation" || op == "transform.world_rotate") {
        auto doc = scene.document();
        const auto view = scene.effective_document();
        const auto e = effective(scene, id);
        if (!e.value("spatial_resolved", false))
            throw CommandError("unresolved_reference", "Spatial parent is unresolved");
        AffineTransform desired{e.at("world_affine").get<std::array<double, 12>>()};
        if (op == "transform.world_translation") {
            const auto& p = a.at("value");
            desired.m[3] = p.at("x");
            desired.m[7] = p.at("y");
            desired.m[11] = p.at("z");
        } else {
            const auto& axis = a.at("axis");
            auto rotation = affine_transform(
                {{},
                 rotation_about_axis({axis.at("x"), axis.at("y"), axis.at("z")}, a.at("degrees")),
                 {}});
            auto rotated = rotation * desired;
            for (unsigned i = 0; i < 3; ++i)
                rotated.m[i * 4 + 3] = desired.m[i * 4 + 3];
            desired = rotated;
        }
        detail::write_world(doc, view, id, desired,
                            op == "transform.world_translation" ? TransformChannel::Translation
                                                                : TransformChannel::Rotation);
        scene.edit(doc);
    } else if (op == "transform.local") {
        auto doc = scene.document();
        const auto current = detail::read_local(effective(scene, id).at("components"));
        auto desired = current;
        unsigned mask = 0;
        if (a.contains("translation")) {
            const auto& p = a.at("translation");
            desired.translation = {p.at("x"), p.at("y"), p.at("z")};
            mask |= 1;
        }
        if (a.contains("rotation")) {
            const auto& p = a.at("rotation");
            desired.rotation = normalized({p.at("x"), p.at("y"), p.at("z"), p.at("w")});
            mask |= 2;
        }
        if (a.contains("scale")) {
            const auto& p = a.at("scale");
            desired.scale = {p.at("x"), p.at("y"), p.at("z")};
            mask |= 4;
        }
        detail::write_local(entity(doc, id), current, desired, TransformChannel(mask));
        scene.edit(doc);
    } else if (op == "entity.duplicate")
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
        auto p = ObjectTransform(e).position;
        if (op == "transform.ground")
            p[1] -= object_bounds(e).first[1];
        else
            for (auto& coordinate : p)
                coordinate = float(std::round(coordinate / a.at("step").get<double>()) *
                                   a.at("step").get<double>());
        auto doc = scene.document();
        AffineTransform desired{e.at("world_affine").get<std::array<double, 12>>()};
        desired.m[3] = p[0];
        desired.m[7] = p[1];
        desired.m[11] = p[2];
        detail::write_world(doc, scene.effective_document(), id, desired,
                            TransformChannel::Translation);
        scene.edit(doc);
    } else if (op == "transform.copy_from") {
        const auto source = effective(scene, a.at("source"));
        auto doc = scene.document();
        detail::write_local(entity(doc, id),
                            detail::read_local(effective(scene, id).at("components")),
                            detail::read_local(source.at("components")), TransformChannel::All);
        scene.edit(doc);
    } else if (op == "component.add") {
        auto doc = scene.document();
        auto& row = entity(doc, id);
        const std::string key = a.at("component");
        bool known = false;
        const auto optional_schema = scene.schema();
        for (const auto& c : optional_schema.at("components"))
            if (c.at("id") == key && c.value("optional", false)) {
                known = true;
                if (effective(scene, id).at("components").contains(key))
                    throw CommandError("unavailable",
                                       "Component already exists; edit or Revert it");
                for (const auto& f : c.at("fields"))
                    row["components"][key][f.at("id").get<std::string>()] = f.at("default");
            }
        if (!known)
            throw CommandError("unsupported_property",
                               "Only optional engine components can be added here");
        scene.edit(doc);
    } else if (op == "component.revert") {
        auto component = a.at("component").get<std::string>();
        if (component == "forge.position")
            component = "forge.local_translation";
        else if (component == "forge.rotation")
            component = "forge.local_rotation";
        else if (component == "forge.scale")
            component = "forge.local_scale";
        const auto schema = scene.schema();
        bool known = false;
        for (const auto& c : schema.at("components"))
            known |= c.at("id") == component;
        if (!known)
            throw CommandError("unsupported_property",
                               "Only supported built-in overrides can be removed");
        auto doc = scene.document();
        auto& row = entity(doc, id);
        row["components"].erase(component);
        if (row.contains("property_overrides"))
            row["property_overrides"].erase(component);
        scene.edit(doc);
    } else if (op == "property.revert") {
        auto doc = scene.document();
        auto& row = entity(doc, id);
        const std::string component = a.at("component"), field = a.at("field");
        if (component.starts_with("forge.local_"))
            throw CommandError("unsupported_property",
                               "Revert this complete transform channel instead");
        (void)property_schema(scene, component, field);
        if (row["components"].contains(component))
            throw CommandError("unsupported_property",
                               "Use component Revert for a full component override");
        if (row.contains("property_overrides") && row["property_overrides"].contains(component)) {
            row["property_overrides"][component].erase(field);
            if (row["property_overrides"][component].empty())
                row["property_overrides"].erase(component);
        }
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
    const Json reparent_mode = {{"type", "string"}, {"enum", {"preserve_world", "keep_local"}}};
    parent["mode"] = reparent_mode;
    add("entity.reparent", "Move to parent",
        "Follow the structural parent spatially. Default preserve_world; keep_local retains local "
        "channels. Empty parent means root.",
        parent, {"entity", "parent"});
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
            "Set this local channel only. Rotation accepts Euler degrees and stores a normalized "
            "quaternion.",
            args, {"entity", "value"});
    }
    auto world_translation = entity_arg;
    world_translation["value"] = xyz();
    add("transform.world_translation", "Move in world space",
        "Own translation only; retain rotation/scale ownership.", world_translation,
        {"entity", "value"});
    auto world_rotation = entity_arg;
    world_rotation["axis"] = xyz();
    world_rotation["degrees"] = number(-360000, 360000);
    add("transform.world_rotate", "Rotate in world space",
        "Rotate about the object origin. Reject required shear or unrelated channel changes.",
        world_rotation, {"entity", "axis", "degrees"});
    auto local = entity_arg;
    local["translation"] = xyz();
    local["scale"] = xyz();
    local["rotation"] = object({{"x", number(-1e38, 1e38)},
                                {"y", number(-1e38, 1e38)},
                                {"z", number(-1e38, 1e38)},
                                {"w", number(-1e38, 1e38)}},
                               {"x", "y", "z", "w"});
    add("transform.local", "Set selected local channels",
        "Only supplied channels become owned; at least one channel is required.", local,
        {"entity"});
    auto binding = entity_arg;
    binding["mode"] = reparent_mode;
    binding["spatial"] =
        object({{"mode", {{"type", "string"}, {"enum", {"world", "follow_structure", "explicit"}}}},
                {"target",
                 object({{"scene", text_type()}, {"entity", text_type()}}, {"scene", "entity"})}},
               {"mode"});
    add("transform.binding", "Set spatial binding",
        "Separate spatial attachment from structural ownership; preserve_world by default.",
        binding, {"entity", "spatial"});
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
        "Copy all effective local channels from a source entity, retaining quaternion rotation.",
        copy, {"entity", "source"});
    auto field = entity_arg;
    field["component"] = text_type();
    field["field"] = text_type();
    field["value"] = {{"anyOf", Json::array({number(-1e38, 1e38), text_type(),
                                             Json{{"type", "boolean"}}, Json{{"type", "null"}}})}};
    add("property.set", "Set reflected property",
        "Validate against the supported reflected property schema.", field,
        {"entity", "component", "field", "value"});
    field.erase("value");
    add("property.revert", "Revert property", "Remove explicit scalar override intent.", field,
        {"entity", "component", "field"});
    add("prefab.instantiate", "Instantiate prefab",
        "Create a linked instance of an available project prefab asset.", {{"asset", text_type()}},
        {"asset"});
    add("prefab.revert_name", "Revert instance name", "Follow the prefab root display name again.",
        entity_arg, {"entity"});
    auto component = entity_arg;
    component["component"] = text_type();
    add("component.add", "Add component", "Add an optional reflected component with its defaults.",
        component, {"entity", "component"});
    add("component.revert", "Remove component override",
        "Remove an owned built-in component; inherited values may become visible.", component,
        {"entity", "component"});
    return commands;
}
static Json prepare_authoring(const Scene& scene, const Json& commands, std::uint64_t revision) {
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
    return {{"document", next}, {"results", results}};
}
Json preview_authoring(const Scene& scene, const Json& commands) {
    return prepare_authoring(scene, commands, scene.revision()).at("document");
}
Json apply_authoring(Scene& scene, const Json& commands, std::uint64_t revision) {
    const auto prepared = prepare_authoring(scene, commands, revision);
    const auto before = scene.revision();
    scene.edit(prepared.at("document"));
    return {{"changed", before != scene.revision()},
            {"revision", scene.revision()},
            {"results", prepared.at("results")}};
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
        if (e.value("missing_member", false))
            add(e, "missing_prefab_member",
                "Prefab member is unavailable. Its mapping and override intent are retained; its "
                "EntityRef is unresolved.");
        if (e.contains("prefab_instance") &&
            e.at("prefab_instance").value("status", "") != "current")
            add(e, "missing_prefab_asset",
                "Prefab asset is unavailable. Restore or relocate the same asset identity and "
                "refresh prefabs.");
        const bool prefab = e.value("prefab", false);
        prefabs += prefab;
        if (!prefab && e.at("components").contains("forge.local_translation") &&
            e.value("spatial_resolved", false))
            ++visible;
        if (names[e.at("name").get<std::string>()] > 1)
            add(e, "duplicate_name", "Display name is shared; stable IDs remain distinct.");
        if (!prefab && !e.at("components").contains("forge.local_translation"))
            add(e, "no_position", "Entity has no effective LocalTranslation and is not drawn.");
        else if (!prefab && !e.value("spatial_resolved", false))
            add(e, "unresolved_spatial_parent",
                "Spatial target is unavailable in this scene; repair its binding to draw the "
                "entity.");
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
    return {{"kind", "scene"}, {"id", scene_.asset_id().str()}, {"session", session_}};
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
