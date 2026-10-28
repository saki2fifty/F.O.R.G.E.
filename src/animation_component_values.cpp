#include "animation_component_values.hpp"
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
        const auto field = std::find_if(fields.begin(), fields.end(), [&](const auto& value) {
            return value.at("id") == binding.field;
        });
        if (field == fields.end() || animation_component_field_type(*field) != binding.value_type ||
            !current.contains(binding.field))
            throw std::runtime_error("Animated field type or edit policy does not match: " +
                                     binding.component + "/" + binding.field);
        if (whole_clip) {
            const auto validate_value = [&](const Json& value) {
                detail::validate_reflected_json(*field, value);
            };
            if (binding.value_type == AnimationComponentValueType::Scalar)
                for (const auto bound : clip.component_range(i))
                    validate_value(bound);
            else
                for (const auto& key : clip.keys)
                    validate_value(key.component_values.at(i).value);
        }
        result[binding.field] = sampled[i];
    }
    detail::validate_reflected_json(structure, result);
    // Match native storage in copied previews as well as the fixed-tick consumer.
    for (const auto& binding : clip.component_bindings)
        if (binding.component == declaration.at("id"))
            for (const auto& field : fields)
                if (field.at("id") == binding.field && field.at("type") == "float32")
                    result[binding.field] = double(float(result.at(binding.field).get<double>()));
    return result;
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
