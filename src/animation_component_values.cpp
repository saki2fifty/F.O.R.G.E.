#include "animation_component_values.hpp"
#include "animation_material_patch.hpp"
#include "reflected_extensions.hpp"
#include <algorithm>
#include <set>

namespace forge {
using Json = nlohmann::json;
std::optional<AnimationComponentValueType> animation_component_field_type(const Json& field) {
    return animation_component_field_type(field.value("type", std::string{}),
                                          field.value("read_only", false));
}
bool animation_component_matches(const Json& declaration, const Json& current) {
    if (!current.is_object() || !declaration.contains("admission"))
        return false;
    const auto& stamp = declaration.at("admission");
    return declaration.value("custom", false) ? current.value("$forge", Json()) == stamp
                                              : stamp.at("format") == "forge.builtin-component";
}
bool animation_component_field_supported(const Json& field) {
    return animation_component_field_type(field).has_value();
}
namespace {
struct FieldTarget {
    const Json* field;
    Json::json_pointer path;
};
FieldTarget resolve(const Json& declaration, const AnimationComponentBinding& binding,
                    const Json* current) {
    if (!declaration.contains("admission") || declaration.at("id") != binding.component ||
        declaration.at("admission") != binding.admission ||
        binding.member_path.size() + binding.element_path.size() > 16)
        throw std::runtime_error("Animated component schema does not match");
    const Json* fields = &declaration.at("fields");
    const Json* field = nullptr;
    auto value = current;
    Json::json_pointer path;
    const auto member = [&](const std::string& name) {
        if (!fields)
            throw std::runtime_error("Animated path requires a struct ancestor");
        const auto at = std::find_if(fields->begin(), fields->end(),
                                     [&](const auto& item) { return item.at("id") == name; });
        if (at == fields->end() || at->value("read_only", false))
            throw std::runtime_error("Animated path is absent or read only: " + name);
        field = &*at;
        if (value) {
            if (!value->is_object() || !value->contains(name))
                throw std::runtime_error("Animated struct member value is absent: " + name);
            value = &value->at(name);
        }
        path /= name;
        fields = field->at("type") == "struct" ? &field->at("fields") : nullptr;
    };
    member(binding.field);
    for (const auto& name : binding.member_path)
        member(name);
    for (const auto& part : binding.element_path) {
        if (part.is_string()) {
            member(part.get_ref<const std::string&>());
            continue;
        }
        const auto& kind = field->at("type");
        if ((kind != "array" && kind != "vector") || !part.is_object() || part.size() != 1)
            throw std::runtime_error("Animated selector requires a collection");
        if (value && !value->is_array())
            throw std::runtime_error("Animated collection value is unavailable");
        const bool keyed = field->contains("element_key") || field->contains("element_keys");
        std::size_t index = 0;
        if (part.contains("index")) {
            const auto& requested = part.at("index");
            const auto maximum = field->at(kind == "array" ? "count" : "maximum_count");
            if (keyed || !requested.is_number_integer() || requested < 0 || requested >= maximum)
                throw std::runtime_error(
                    "Animated positional selector does not match collection policy");
            index = requested.get<std::size_t>();
        } else if (part.contains("key") && keyed) {
            const auto& key = part.at("key");
            const bool composite = field->contains("element_keys");
            if (composite ? !key.is_array() || key.size() != field->at("element_keys").size()
                          : !key.is_string())
                throw std::runtime_error("Animated key does not match its declared collection key");
            const auto identity = composite ? key.dump() : key.get<std::string>();
            if (value) {
                std::optional<std::size_t> selected;
                for (std::size_t i = 0; i < value->size(); ++i)
                    if (detail::reflected_sequence_identity(*field, value->at(i)) == identity) {
                        if (selected)
                            throw std::runtime_error("Animated collection key is ambiguous");
                        selected = i;
                    }
                if (!selected)
                    throw std::runtime_error("Animated collection key is missing");
                index = *selected;
            }
        } else
            throw std::runtime_error("Animated selector does not match collection policy");
        if (value) {
            if (index >= value->size())
                throw std::runtime_error("Animated collection index is missing");
            value = &value->at(index);
        }
        path /= std::to_string(index);
        field = &field->at("element");
        if (field->value("read_only", false))
            throw std::runtime_error("Animated collection element is read only");
        fields = field->at("type") == "struct" ? &field->at("fields") : nullptr;
    }
    if (animation_component_field_type(*field) != binding.value_type)
        throw std::runtime_error("Animated leaf type does not match: " +
                                 animation_component_label(binding));
    return {field, std::move(path)};
}
} // namespace
const Json& animation_component_field(const Json& declaration,
                                      const AnimationComponentBinding& binding) {
    return *resolve(declaration, binding, nullptr).field;
}
Json::json_pointer animation_component_pointer(const Json& declaration, const Json& current,
                                               const AnimationComponentBinding& binding) {
    return resolve(declaration, binding, &current).path;
}
std::vector<AnimationComponentBinding> animation_component_fields(const Json& declaration,
                                                                  const Json* current) {
    std::vector<AnimationComponentBinding> result;
    if (!declaration.contains("admission"))
        return result;
    AnimationComponentBinding binding{declaration.at("id"), "", declaration.at("admission")};
    const auto visit = [&](const auto& self, const Json& field, const Json* value) -> void {
        if (field.value("read_only", false))
            return;
        if (const auto kind = animation_component_field_type(field)) {
            binding.value_type = *kind;
            result.push_back(binding);
            return;
        }
        if (binding.member_path.size() + binding.element_path.size() >= 16)
            return;
        if (field.at("type") == "struct") {
            for (const auto& child : field.at("fields")) {
                const auto name = child.at("id").get<std::string>();
                const bool elements = !binding.element_path.empty();
                if (elements)
                    binding.element_path.push_back(name);
                else
                    binding.member_path.push_back(name);
                self(self, child, value ? &value->at(name) : nullptr);
                if (elements)
                    binding.element_path.pop_back();
                else
                    binding.member_path.pop_back();
            }
        } else if ((field.at("type") == "array" || field.at("type") == "vector") && value) {
            for (std::size_t i = 0; i < value->size(); ++i) {
                Json selector;
                if (field.contains("element_key") || field.contains("element_keys")) {
                    const auto key = detail::reflected_sequence_identity(field, value->at(i));
                    selector = {
                        {"key", field.contains("element_keys") ? Json::parse(key) : Json(key)}};
                } else
                    selector = {{"index", i}};
                binding.element_path.push_back(std::move(selector));
                self(self, field.at("element"), &value->at(i));
                binding.element_path.pop_back();
            }
        }
    };
    for (const auto& field : declaration.at("fields")) {
        binding.field = field.at("id");
        visit(visit, field, current ? &current->at(binding.field) : nullptr);
    }
    return result;
}
Json sample_animation_component(const Json& declaration, const Json& current,
                                const TransformAnimationClip& clip, double time, bool whole_clip,
                                std::span<const Json> samples) {
    if (!animation_component_matches(declaration, current))
        throw std::runtime_error("Animated component requires its matching authored schema");
    const auto& fields = declaration.at("fields");
    const Json structure{{"type", "struct"}, {"fields", fields}};
    auto result = current;
    std::array<Json, 64> sampled;
    if (clip.component_bindings.size() > sampled.size())
        throw std::runtime_error("Animated component binding count exceeds64");
    if (samples.empty()) {
        clip.sample_component_values(time,
                                     std::span(sampled).first(clip.component_bindings.size()));
        samples = std::span(sampled).first(clip.component_bindings.size());
    }
    if (samples.size() != clip.component_bindings.size())
        throw std::runtime_error("Animated component sample count differs from its bindings");
    for (std::size_t i = 0; i < clip.component_bindings.size(); ++i) {
        const auto& binding = clip.component_bindings[i];
        if (Json(binding.component) != declaration.at("id"))
            continue;
        if (binding.admission != declaration.at("admission"))
            throw std::runtime_error("Animated component schema has changed: " + binding.component);
        const auto target = resolve(declaration, binding, &current);
        const auto& field = *target.field;
        const auto& path = target.path;
        if (!current.contains(path))
            throw std::runtime_error("Animated component leaf value is absent");
        if (whole_clip) {
            const auto validate_value = [&](const Json& value) {
                detail::validate_reflected_json(field, value);
            };
            if (binding.value_type == AnimationComponentValueType::Scalar)
                for (const auto bound : clip.component_range(i))
                    validate_value(bound);
            else
                for (const auto& key : clip.keys)
                    validate_value(key.component_values.at(i).value);
        }
        result.at(path) = samples[i];
        if (field.at("type") == "float32")
            result.at(path) = double(float(result.at(path).get<double>()));
    }
    detail::validate_reflected_json(structure, result);
    return result;
}
void merge_animation_properties(Json& components, const Json& patch, const WorldContext& context) {
    for (const auto& [component, fields] : patch.items()) {
        const auto& codecs = context.authored_codecs();
        const auto codec = std::find_if(codecs.begin(), codecs.end(),
                                        [&](const auto& item) { return item.key() == component; });
        for (const auto& [field, value] : fields.items()) {
            if (component == "forge.mesh_renderer" && field == "material_parameters") {
                merge_animation_material_parameters(components[component], value);
                continue;
            }
            auto& target = components[component][field];
            if (codec == codecs.end()) {
                const auto& builtins = detail::builtins();
                const auto builtin =
                    std::find_if(builtins.begin(), builtins.end(),
                                 [&](const auto& item) { return component == item.name; });
                target =
                    builtin == builtins.end()
                        ? value
                        : detail::merge_builtin_property_extensions(*builtin, field, value, target);
                continue;
            }
            const auto& members = codec->declaration.at("structure").at("fields");
            const auto member = std::find_if(members.begin(), members.end(), [&](const auto& item) {
                return item.at("id") == field;
            });
            if (member == members.end())
                throw std::runtime_error("Animated projection member is unavailable: " + field);
            target = detail::merge_reflected_extensions(
                *member, value, detail::reflected_extensions(*member, target));
        }
    }
}
std::vector<AnimationComponentCandidate>
prepare_animation_components(WorldContext& context, flecs::entity entity,
                             const TransformAnimationClip& clip, double time, bool whole_clip,
                             std::span<const Json> samples) {
    std::vector<AnimationComponentCandidate> result;
    if (clip.component_bindings.empty())
        return result;
    std::set<std::string> remaining;
    for (const auto& binding : clip.component_bindings)
        remaining.insert(binding.component);
    for (const auto& codec : context.authored_codecs()) {
        if (!remaining.erase(codec.key()))
            continue;
        auto value = sample_animation_component(codec.editor_schema(), codec.read(entity, true),
                                                clip, time, whole_clip, samples);
        auto prepared = codec.prepare_update(entity, value);
        if (!prepared)
            throw std::runtime_error("Animated component schema admission failed");
        auto patch = Json::object();
        for (const auto& binding : clip.component_bindings)
            if (binding.component == codec.key())
                patch[binding.field] = value.at(binding.field);
        result.push_back({&codec, std::move(prepared), std::move(patch), nullptr, {}});
    }
    for (const auto& builtin : detail::builtins()) {
        if (!remaining.contains(builtin.name))
            continue;
        const auto& types = context.schema().at("components");
        const auto declaration = std::find_if(types.begin(), types.end(), [&](const auto& type) {
            return type.at("id") == builtin.name;
        });
        if (declaration == types.end() || !declaration->contains("admission"))
            continue;
        auto value = sample_animation_component(*declaration, builtin.read(entity, true), clip,
                                                time, whole_clip, samples);
        detail::validate_components({{builtin.name, value}});
        auto typed = builtin.decode(value);
        std::visit([&](const auto& item) { detail::validate_reflected_value(entity, item); },
                   typed);
        auto patch = Json::object();
        for (const auto& binding : clip.component_bindings)
            if (binding.component == builtin.name)
                patch[binding.field] = value.at(binding.field);
        result.push_back({nullptr, {}, std::move(patch), &builtin, std::move(typed)});
        remaining.erase(builtin.name);
    }
    if (!remaining.empty())
        throw std::runtime_error("Animated component is unavailable: " + *remaining.begin());
    return result;
}
std::string AnimationComponentCandidate::key() const {
    return builtin ? builtin->name : codec->key();
}
void AnimationComponentCandidate::apply(flecs::entity entity) const {
    if (builtin)
        builtin->apply(entity, builtin_value);
    else
        codec->apply(entity, value);
}
Json animation_component_patch(const std::vector<AnimationComponentCandidate>& candidates) {
    auto result = Json::object();
    for (const auto& candidate : candidates)
        result[candidate.key()] = candidate.patch;
    return result;
}
} // namespace forge
