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
bool animation_component_field_supported(const Json& field) {
    return animation_component_field_type(field).has_value();
}
const Json& animation_component_field(const Json& declaration,
                                      const AnimationComponentBinding& binding) {
    if (!declaration.value("custom", false) || declaration.at("id") != binding.component ||
        declaration.at("admission") != binding.admission || binding.member_path.size() > 16)
        throw std::runtime_error("Animated component schema does not match");
    const Json* fields = &declaration.at("fields");
    const Json* result = nullptr;
    const auto member = [&](const std::string& name) {
        if (!fields)
            throw std::runtime_error("Animated path requires a struct ancestor");
        const auto at = std::find_if(fields->begin(), fields->end(),
                                     [&](const auto& field) { return field.at("id") == name; });
        if (at == fields->end() || at->value("read_only", false))
            throw std::runtime_error("Animated path is absent or read only: " + name);
        result = &*at;
        fields = result->at("type") == "struct" ? &result->at("fields") : nullptr;
    };
    member(binding.field);
    for (const auto& name : binding.member_path)
        member(name);
    if (animation_component_field_type(*result) != binding.value_type)
        throw std::runtime_error("Animated leaf type does not match: " +
                                 animation_component_label(binding));
    return *result;
}
std::vector<AnimationComponentBinding> animation_component_fields(const Json& declaration) {
    std::vector<AnimationComponentBinding> result;
    if (!declaration.value("custom", false))
        return result;
    AnimationComponentBinding binding{declaration.at("id"), "", declaration.at("admission")};
    const auto visit = [&](const auto& self, const Json& field) -> void {
        if (field.value("read_only", false))
            return;
        if (const auto kind = animation_component_field_type(field)) {
            binding.value_type = *kind;
            result.push_back(binding);
        } else if (field.at("type") == "struct" && binding.member_path.size() < 16)
            for (const auto& child : field.at("fields")) {
                binding.member_path.push_back(child.at("id"));
                self(self, child);
                binding.member_path.pop_back();
            }
    };
    for (const auto& field : declaration.at("fields")) {
        binding.field = field.at("id");
        visit(visit, field);
    }
    return result;
}
Json sample_animation_component(const Json& declaration, const Json& current,
                                const TransformAnimationClip& clip, double time, bool whole_clip) {
    if (!declaration.value("custom", false) || !current.is_object() ||
        current.value("$forge", Json()) != declaration.at("admission"))
        throw std::runtime_error("Animated component requires its matching authored schema");
    const auto& fields = declaration.at("fields");
    const Json structure{{"type", "struct"}, {"fields", fields}};
    auto result = current;
    std::array<Json, 64> sampled;
    if (clip.component_bindings.size() > sampled.size())
        throw std::runtime_error("Animated component binding count exceeds64");
    clip.sample_component_values(time, std::span(sampled).first(clip.component_bindings.size()));
    for (std::size_t i = 0; i < clip.component_bindings.size(); ++i) {
        const auto& binding = clip.component_bindings[i];
        if (binding.component != declaration.at("id"))
            continue;
        if (binding.admission != declaration.at("admission"))
            throw std::runtime_error("Animated component schema has changed: " + binding.component);
        const auto& field = animation_component_field(declaration, binding);
        const auto path = animation_component_pointer(binding);
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
        result.at(path) = sampled[i];
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
                target = value;
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
                             const TransformAnimationClip& clip, double time, bool whole_clip) {
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
                                                clip, time, whole_clip);
        auto prepared = codec.prepare_update(entity, value);
        if (!prepared)
            throw std::runtime_error("Animated component schema admission failed");
        auto patch = Json::object();
        for (const auto& binding : clip.component_bindings)
            if (binding.component == codec.key())
                patch[binding.field] = value.at(binding.field);
        result.push_back({&codec, std::move(prepared), std::move(patch)});
    }
    if (!remaining.empty())
        throw std::runtime_error("Animated component is unavailable: " + *remaining.begin());
    return result;
}
Json animation_component_patch(const std::vector<AnimationComponentCandidate>& candidates) {
    auto result = Json::object();
    for (const auto& candidate : candidates)
        result[candidate.codec->key()] = candidate.patch;
    return result;
}
} // namespace forge
