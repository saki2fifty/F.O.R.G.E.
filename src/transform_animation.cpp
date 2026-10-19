#include "asset_bytes.hpp"
#include "bounded_json.hpp"
#include "material_parameter_values.hpp"
#include "material_slot.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <forge/transform_animation.hpp>
#include <limits>
#include <set>
#include <string_view>
#include <tuple>
namespace forge {
using Json = nlohmann::json;
namespace {
void require(bool ok, const char* why) {
    if (!ok)
        throw std::runtime_error(why);
}
std::int64_t time_value(const nlohmann::json& value) {
    require(value.is_number_integer(), "Animation times must be integer microseconds");
    const auto result = value.get<std::int64_t>();
    require(result >= 0 && result <= 3600 * animation_timebase, "Animation time exceeds one hour");
    return result;
}
template <std::size_t N> std::array<double, N> numbers(const nlohmann::json& value) {
    require(value.is_array() && value.size() == N, "Invalid animation channel width");
    std::array<double, N> result;
    for (std::size_t i = 0; i < N; ++i) {
        require(value[i].is_number(), "Animation channel must be numeric");
        result[i] = value[i].get<double>();
        require(std::isfinite(result[i]) && std::abs(result[i]) <= 1e9,
                "Animation channel is nonfinite or out of range");
    }
    return result;
}
struct Cubic {
    double a, b, c, d, endpoint;
    double at(double t) const {
        if (t == 0)
            return d;
        if (t == 1)
            return endpoint;
        return ((a * t + b) * t + c) * t + d;
    }
    std::array<double, 2> bounds() const {
        std::array<double, 2> range{std::min(at(0), at(1)), std::max(at(0), at(1))};
        auto include = [&](double t) {
            if (t > 0 && t < 1) {
                const auto y = at(t);
                range[0] = std::min(range[0], y);
                range[1] = std::max(range[1], y);
            }
        };
        const auto qa = 3 * a, qb = 2 * b, qc = c;
        if (qa == 0) {
            if (qb != 0)
                include(-qc / qb);
            return range;
        }
        const auto discriminant = qb * qb - 4 * qa * qc;
        if (discriminant < 0)
            return range;
        const auto q = -.5 * (qb + std::copysign(std::sqrt(discriminant), qb));
        if (q == 0)
            include(-qb / (2 * qa));
        else {
            include(q / qa);
            include(qc / q);
        }
        return range;
    }
};
Cubic segment(double p0, double p1, double m0, double m1, double seconds) {
    const auto left = seconds * m0, right = seconds * m1;
    return {2 * p0 - 2 * p1 + left + right, -3 * p0 + 3 * p1 - 2 * left - right, left, p0, p1};
}

std::pair<double, double> tangent_slopes(double left, double right, double before, double after,
                                         TransformAnimationTangentPreset preset) {
    if (!before)
        left = right;
    if (!after)
        right = left;
    if (preset == TransformAnimationTangentPreset::Flat)
        left = right = 0;
    else if (preset == TransformAnimationTangentPreset::Smooth) {
        double slope = before && after ? (left * after + right * before) / (before + after)
                       : before        ? left
                                       : right;
        if (left == 0 || right == 0 || std::signbit(left) != std::signbit(right))
            slope = 0;
        else
            slope = std::copysign(
                std::min(std::abs(slope), 3 * std::min(std::abs(left), std::abs(right))), left);
        left = right = slope;
    }
    return {left, right};
}

TransformAnimationTangents tangent(const Json& object, const char* name) {
    const auto& data = object.at(name);
    return {numbers<3>(data.at("translation")), numbers<3>(data.at("scale"))};
}
Json tangent_json(const TransformAnimationTangents& value) {
    return {{"translation", {value.translation[0], value.translation[1], value.translation[2]}},
            {"scale", {value.scale[0], value.scale[1], value.scale[2]}}};
}
Json curves_json(const TransformAnimationCurves& value) {
    return {{"interpolation", value.cubic ? "cubic" : "linear"},
            {"incoming", tangent_json(value.incoming)},
            {"outgoing", tangent_json(value.outgoing)}};
}
void merge_curve_settings(Json& key, const TransformAnimationCurves& value) {
    const auto encoded = curves_json(value);
    if (!key.contains("curves"))
        key["curves"] = Json::object();
    auto& target = key["curves"];
    target["interpolation"] = encoded.at("interpolation");
    for (const char* side : {"incoming", "outgoing"}) {
        if (!target.contains(side))
            target[side] = Json::object();
        for (const char* field : {"translation", "scale"})
            target[side][field] = encoded.at(side).at(field);
    }
    for (const auto& binding : animation_property_bindings()) {
        if (!(value.property_mask & animation_property_bit(binding.property)))
            continue;
        const auto& settings = value.properties[unsigned(binding.property)];
        auto& fields = target["property_curves"][binding.name];
        fields["interpolation"] = settings.cubic ? "cubic" : "linear";
        fields["incoming"] = settings.incoming;
        fields["outgoing"] = settings.outgoing;
    }
}
void promote_property_curves(Json& document) {
    if (document.at("version") < 4)
        for (const auto& key : document.at("keys"))
            require(!key.contains("curves") || !key.at("curves").contains("property_curves"),
                    "Legacy property_curves extension collides with version4; source unchanged");
    document["version"] = std::max(4, document.at("version").get<int>());
}
TransformAnimationCurves curves(const Json& key, bool scalar_enabled) {
    if (!key.contains("curves"))
        return {};
    const auto& object = key.at("curves");
    const auto mode = object.at("interpolation");
    require(mode == "linear" || mode == "cubic", "Invalid vector curve interpolation");
    TransformAnimationCurves result{mode == "cubic", tangent(object, "incoming"),
                                    tangent(object, "outgoing")};
    if (scalar_enabled && object.contains("property_curves")) {
        const auto& map = object.at("property_curves");
        require(map.is_object() && map.size() <= animation_property_count,
                "Invalid property curve map");
        for (const auto& [name, settings] : map.items()) {
            const auto property = animation_property_named(name);
            require(!animation_property_discrete(property),
                    "Discrete animated properties do not support curve settings");
            const auto interpolation = settings.at("interpolation");
            require(interpolation == "linear" || interpolation == "cubic",
                    "Invalid property curve interpolation");
            auto slope = [&](const char* field) {
                const auto& raw = settings.at(field);
                require(raw.is_number(), "Property curve slopes must be numeric");
                const auto value = raw.get<double>();
                require(std::isfinite(value) && std::abs(value) <= 1e9,
                        "Property curve slope outside source bounds");
                return value;
            };
            result.property_mask |= animation_property_bit(property);
            result.properties[unsigned(property)] = {interpolation == "cubic", slope("incoming"),
                                                     slope("outgoing")};
        }
    }
    return result;
}
AnimationMaterialBinding material_binding(const Json& row) {
    AnimationMaterialBinding result{row.at("slot").get<std::string>(),
                                    row.at("key").get<std::string>()};
    detail::validate_material_slot_key(result.slot);
    detail::validate_material_key(result.key);
    const auto& type = row.at("type");
    require(type.is_number_integer() && type >= 0 &&
                type <= unsigned(MaterialParameterType::LinearColor4),
            "Invalid animated material parameter type");
    result.type = MaterialParameterType(type.get<unsigned>());
    return result;
}
Json material_numbers_json(const AnimationMaterialBinding& binding,
                           const AnimationMaterialValues& values) {
    const auto width = material_parameter_width(binding.type);
    auto result = Json::array();
    for (unsigned lane = 0; lane < 4; ++lane) {
        require(std::isfinite(values[lane]) && std::abs(values[lane]) <= 1e9 &&
                    (lane < width || values[lane] == 0),
                "Invalid animated material value or unused lane");
        if (lane < width)
            result.push_back(values[lane]);
    }
    return result;
}
bool same_material_binding(const Json& row, std::string_view slot, std::string_view name) {
    return row.at("slot").get_ref<const std::string&>() == slot &&
           row.at("key").get_ref<const std::string&>() == name;
}
std::size_t material_binding_index(const TransformAnimationClip& clip, std::string_view slot,
                                   std::string_view name) {
    for (std::size_t index = 0; index < clip.material_bindings.size(); ++index)
        if (clip.material_bindings[index].slot == slot && clip.material_bindings[index].key == name)
            return index;
    throw std::runtime_error("Animated material binding is not declared");
}
void promote_material_parameters(Json& document) {
    if (document.at("version") < 5) {
        require(!document.contains("material_bindings"),
                "Legacy material_bindings extension collides with version5; source unchanged");
        for (const auto& key : document.at("keys"))
            require(!key.contains("material_values"),
                    "Legacy material_values extension collides with version5; source unchanged");
    }
    // Promoting to5 also crosses the scalar-curve version boundary. Older
    // property_curves extensions must not silently become active animation.
    promote_property_curves(document);
    document["version"] = 5;
}
void merge_material_values(Json& key, std::span<const AnimationMaterialBinding> bindings,
                           std::span<const AnimationMaterialValues> values) {
    require(bindings.size() == values.size(), "Animated material sample count differs");
    auto& rows = key["material_values"];
    if (rows.is_null())
        rows = Json::array();
    for (std::size_t i = 0; i < bindings.size(); ++i) {
        const auto& binding = bindings[i];
        const auto encoded = material_numbers_json(binding, values[i]);
        auto found = std::find_if(rows.begin(), rows.end(), [&](const auto& row) {
            return same_material_binding(row, binding.slot, binding.key);
        });
        if (found == rows.end())
            rows.push_back({{"slot", binding.slot}, {"key", binding.key}, {"value", encoded}});
        else
            (*found)["value"] = encoded;
    }
}
std::vector<AnimationMaterialKey>
material_keys(const Json& key, std::span<const AnimationMaterialBinding> bindings) {
    if (bindings.empty() && !key.contains("material_values"))
        return {};
    require(key.contains("material_values") && key.at("material_values").is_array() &&
                key.at("material_values").size() == bindings.size(),
            "Animated material key needs every declared binding exactly once");
    std::vector<AnimationMaterialKey> result(bindings.size());
    std::set<std::size_t> seen;
    for (const auto& row : key.at("material_values")) {
        const auto slot = row.at("slot").get<std::string>();
        const auto name = row.at("key").get<std::string>();
        const auto found = std::find_if(bindings.begin(), bindings.end(), [&](const auto& value) {
            return value.slot == slot && value.key == name;
        });
        require(found != bindings.end(), "Animated material key has an undeclared binding");
        const auto index = std::size_t(found - bindings.begin());
        require(seen.insert(index).second, "Duplicate animated material key binding");
        const auto width = material_parameter_width(found->type);
        const auto& values = row.at("value");
        require(values.is_array() && values.size() == width,
                "Invalid animated material parameter width");
        for (unsigned lane = 0; lane < width; ++lane) {
            require(values[lane].is_number(), "Animated material value must be numeric");
            const auto value = values[lane].get<double>();
            require(std::isfinite(value) && std::abs(value) <= 1e9,
                    "Animated material value exceeds source bounds");
            result[index].values[lane] = value;
        }
        if (!row.contains("curves"))
            continue;
        const auto& curves = row.at("curves");
        require(curves.is_array() && curves.size() == width,
                "Invalid animated material curve width");
        for (unsigned lane = 0; lane < width; ++lane) {
            const auto& curve = curves[lane];
            const auto& mode = curve.at("interpolation");
            require(mode == "linear" || mode == "cubic",
                    "Invalid animated material curve interpolation");
            auto slope = [&](const char* field) {
                const auto& raw = curve.at(field);
                require(raw.is_number(), "Animated material slope must be numeric");
                const auto value = raw.get<double>();
                require(std::isfinite(value) && std::abs(value) <= 1e9,
                        "Animated material slope exceeds source bounds");
                return value;
            };
            result[index].curves[lane] = {mode == "cubic", slope("incoming"), slope("outgoing")};
        }
    }
    return result;
}
void validate_curve_segments(const TransformAnimationClip& clip) {
    for (std::size_t i = 1; i < clip.keys.size(); ++i) {
        const auto& left = clip.keys[i - 1];
        const auto& right = clip.keys[i];
        if (left.held)
            continue;
        const auto seconds = double(right.time - left.time) / animation_timebase;
        for (unsigned axis = 0; axis < 3; ++axis) {
            if (left.curves.cubic && (clip.channels & 1)) {
                const auto range =
                    segment(std::array<double, 3>{left.pose.translation.x, left.pose.translation.y,
                                                  left.pose.translation.z}[axis],
                            std::array<double, 3>{right.pose.translation.x,
                                                  right.pose.translation.y,
                                                  right.pose.translation.z}[axis],
                            left.curves.outgoing.translation[axis],
                            right.curves.incoming.translation[axis], seconds)
                        .bounds();
                require(range[0] >= -1e9 && range[1] <= 1e9,
                        "Translation curve overshoots source bounds");
            }
            if (left.curves.cubic && (clip.channels & 4)) {
                const std::array<float, 3> a{left.pose.scale.x, left.pose.scale.y,
                                             left.pose.scale.z},
                    b{right.pose.scale.x, right.pose.scale.y, right.pose.scale.z};
                const auto range = segment(a[axis], b[axis], left.curves.outgoing.scale[axis],
                                           right.curves.incoming.scale[axis], seconds)
                                       .bounds();
                require(range[0] >= -max_local_scale && range[1] <= max_local_scale,
                        "Scale curve overshoots visual bounds");
            }
        }
        for (const auto& binding : animation_property_bindings()) {
            const auto property = binding.property;
            const auto index = unsigned(property);
            if (!(left.curves.property_mask & animation_property_bit(property)) ||
                !left.curves.properties[index].cubic)
                continue;
            const auto range = segment(left.properties[index], right.properties[index],
                                       left.curves.properties[index].outgoing,
                                       right.curves.properties[index].incoming, seconds)
                                   .bounds();
            validate_animation_property(property, range[0]);
            validate_animation_property(property, range[1]);
        }
        for (std::size_t binding = 0; binding < clip.material_bindings.size(); ++binding) {
            const auto width = material_parameter_width(clip.material_bindings[binding].type);
            const auto& a = left.materials[binding];
            const auto& b = right.materials[binding];
            for (unsigned lane = 0; lane < width; ++lane) {
                if (!a.curves[lane].cubic)
                    continue;
                const auto range = segment(a.values[lane], b.values[lane], a.curves[lane].outgoing,
                                           b.curves[lane].incoming, seconds)
                                       .bounds();
                require(range[0] >= -1e9 && range[1] <= 1e9,
                        "Material curve overshoots source bounds");
            }
        }
    }
}
LocalRotation slerp(LocalRotation a, LocalRotation b, double t) {
    double dot = double(a.x) * b.x + double(a.y) * b.y + double(a.z) * b.z + double(a.w) * b.w;
    if (dot < 0) {
        b = {-b.x, -b.y, -b.z, -b.w};
        dot = -dot;
    }
    double left = 1 - t, right = t;
    if (dot < .9995) {
        const double angle = std::acos(std::clamp(dot, -1., 1.));
        left = std::sin((1 - t) * angle) / std::sin(angle);
        right = std::sin(t * angle) / std::sin(angle);
    }
    return normalized({float(left * a.x + right * b.x), float(left * a.y + right * b.y),
                       float(left * a.z + right * b.z), float(left * a.w + right * b.w)});
}
} // namespace
nlohmann::json animation_pose_json(const LocalTransform& p) {
    return {{"translation", {p.translation.x, p.translation.y, p.translation.z}},
            {"rotation", {p.rotation.x, p.rotation.y, p.rotation.z, p.rotation.w}},
            {"scale", {p.scale.x, p.scale.y, p.scale.z}}};
}
LocalTransform animation_pose(const nlohmann::json& value) {
    const auto p = numbers<3>(value.at("translation"));
    const auto q = numbers<4>(value.at("rotation"));
    const double length = q[0] * q[0] + q[1] * q[1] + q[2] * q[2] + q[3] * q[3];
    require(std::abs(length - 1) <= 1e-5, "Animation quaternion must be normalized");
    return {{p[0], p[1], p[2]},
            normalized({float(q[0]), float(q[1]), float(q[2]), float(q[3])}),
            checked_local_scale(numbers<3>(value.at("scale")))};
}
std::size_t TransformAnimationClip::resident_bytes() const {
    std::size_t bytes = sizeof(*this) + keys.capacity() * sizeof(TransformAnimationKey) +
                        material_bindings.capacity() * sizeof(AnimationMaterialBinding);
    for (const auto& binding : material_bindings)
        bytes += binding.slot.capacity() + binding.key.capacity() + 2;
    for (const auto& key : keys)
        bytes += key.materials.capacity() * sizeof(AnimationMaterialKey);
    return bytes;
}
void TransformAnimationClip::sample_materials(double time,
                                              std::span<AnimationMaterialValues> output) const {
    require(std::isfinite(time) && !keys.empty(), "Invalid material animation sample time/keys");
    require(output.size() == material_bindings.size(), "Invalid material animation sample buffer");
    if (output.empty())
        return;
    const auto right =
        std::upper_bound(keys.begin(), keys.end(), time,
                         [](double value, const auto& key) { return value < key.time; });
    const auto& left = right == keys.begin() ? *right : *(right - 1);
    require(left.materials.size() == output.size() &&
                (right == keys.end() || right->materials.size() == output.size()),
            "Material animation sample bindings differ from keys");
    const bool endpoint = right == keys.begin() || right == keys.end() || left.held;
    const double t =
        endpoint ? 0 : std::clamp((time - left.time) / double(right->time - left.time), 0., 1.);
    for (std::size_t binding = 0; binding < output.size(); ++binding) {
        output[binding] = {};
        const auto width = material_parameter_width(material_bindings[binding].type);
        const auto& a = left.materials[binding];
        for (unsigned lane = 0; lane < width; ++lane) {
            double value = a.values[lane];
            if (!endpoint) {
                const auto& b = right->materials[binding];
                if (a.curves[lane].cubic) {
                    const double seconds = double(right->time - left.time) / animation_timebase;
                    value = t <= .5
                                ? segment(a.values[lane], b.values[lane], a.curves[lane].outgoing,
                                          b.curves[lane].incoming, seconds)
                                      .at(t)
                                : segment(b.values[lane], a.values[lane], -b.curves[lane].incoming,
                                          -a.curves[lane].outgoing, seconds)
                                      .at(1 - t);
                } else
                    value = std::lerp(a.values[lane], b.values[lane], t);
            }
            require(std::isfinite(value) &&
                        std::abs(value) <= 1e9 * (1 + 64 * std::numeric_limits<double>::epsilon()),
                    "Animated material sample exceeds source bounds");
            output[binding][lane] = std::clamp(value, -1e9, 1e9);
        }
    }
}
std::array<double, 2> TransformAnimationClip::material_range(std::size_t binding,
                                                             unsigned lane) const {
    require(!keys.empty() && binding < material_bindings.size(),
            "Invalid material animation range binding/keys");
    require(lane < material_parameter_width(material_bindings[binding].type),
            "Invalid material animation range lane");
    const auto first = keys.front().materials.at(binding).values[lane];
    std::array<double, 2> range{first, first};
    for (std::size_t i = 1; i < keys.size(); ++i) {
        const auto& left = keys[i - 1];
        const auto& right = keys[i];
        const auto& a = left.materials.at(binding);
        const auto& b = right.materials.at(binding);
        auto interval = std::array<double, 2>{std::min(a.values[lane], b.values[lane]),
                                              std::max(a.values[lane], b.values[lane])};
        if (!left.held && a.curves[lane].cubic)
            interval = segment(a.values[lane], b.values[lane], a.curves[lane].outgoing,
                               b.curves[lane].incoming,
                               double(right.time - left.time) / animation_timebase)
                           .bounds();
        range[0] = std::min(range[0], interval[0]);
        range[1] = std::max(range[1], interval[1]);
    }
    return range;
}
AnimationPropertyValues TransformAnimationClip::sample_properties(double time) const {
    require(std::isfinite(time) && !keys.empty(), "Invalid property animation sample time/keys");
    if (!properties)
        return {};
    const auto right =
        std::upper_bound(keys.begin(), keys.end(), time,
                         [](double value, const auto& key) { return value < key.time; });
    if (right == keys.begin())
        return right->properties;
    const auto& left = *(right - 1);
    if (right == keys.end() || left.held)
        return left.properties;
    const double t = std::clamp((time - left.time) / double(right->time - left.time), 0., 1.);
    AnimationPropertyValues result{};
    for (unsigned i = 0; i < animation_property_count; ++i)
        if (properties & (1u << i)) {
            if (animation_property_discrete(AnimationProperty(i))) {
                result[i] = left.properties[i];
                continue;
            }
            const auto& curve = left.curves.properties[i];
            if ((left.curves.property_mask & (1u << i)) && curve.cubic) {
                const auto seconds = double(right->time - left.time) / animation_timebase;
                // Evaluate from the nearer endpoint. This retains tiny positive
                // camera heights beside a large value instead of cancelling them.
                const auto value =
                    t <= .5
                        ? segment(left.properties[i], right->properties[i], curve.outgoing,
                                  right->curves.properties[i].incoming, seconds)
                              .at(t)
                        : segment(right->properties[i], left.properties[i],
                                  -right->curves.properties[i].incoming, -curve.outgoing, seconds)
                              .at(1 - t);
                result[i] = animation_property_curve_sample(AnimationProperty(i), value);
            } else
                result[i] = std::lerp(left.properties[i], right->properties[i], t);
        }
    return result;
}
LocalTransform TransformAnimationClip::sample(double time) const {
    require(std::isfinite(time) && !keys.empty(), "Invalid animation sample time/keys");
    const auto right =
        std::upper_bound(keys.begin(), keys.end(), time,
                         [](double value, const auto& key) { return value < key.time; });
    if (right == keys.begin())
        return right->pose;
    const auto& left = *(right - 1);
    if (right == keys.end() || left.held)
        return left.pose;
    const double t = std::clamp((time - left.time) / double(right->time - left.time), 0., 1.);
    const auto& a = left.pose;
    const auto& b = right->pose;
    auto mix = [t](double x, double y) { return x + (y - x) * t; };
    if (left.curves.cubic && (channels & 5)) {
        const auto seconds = double(right->time - left.time) / animation_timebase;
        std::array<double, 3> translation{a.translation.x, a.translation.y, a.translation.z};
        auto scale = a.scale;
        const std::array<float, 3> av{a.scale.x, a.scale.y, a.scale.z},
            bv{b.scale.x, b.scale.y, b.scale.z};
        std::array<float, 3> sv{};
        for (unsigned axis = 0; axis < 3; ++axis) {
            const std::array<double, 3> at{a.translation.x, a.translation.y, a.translation.z},
                bt{b.translation.x, b.translation.y, b.translation.z};
            if (channels & 1)
                translation[axis] = segment(std::array<double, 3>{a.translation.x, a.translation.y,
                                                                  a.translation.z}[axis],
                                            std::array<double, 3>{b.translation.x, b.translation.y,
                                                                  b.translation.z}[axis],
                                            left.curves.outgoing.translation[axis],
                                            right->curves.incoming.translation[axis], seconds)
                                        .at(t);
            else
                translation[axis] = mix(at[axis], bt[axis]);
            sv[axis] = (channels & 4)
                           ? float(segment(av[axis], bv[axis], left.curves.outgoing.scale[axis],
                                           right->curves.incoming.scale[axis], seconds)
                                       .at(t))
                           : float(mix(av[axis], bv[axis]));
        }
        scale = {sv[0], sv[1], sv[2]};
        return {{translation[0], translation[1], translation[2]},
                slerp(a.rotation, b.rotation, t),
                scale};
    }
    return {{mix(a.translation.x, b.translation.x), mix(a.translation.y, b.translation.y),
             mix(a.translation.z, b.translation.z)},
            slerp(a.rotation, b.rotation, t),
            {float(mix(a.scale.x, b.scale.x)), float(mix(a.scale.y, b.scale.y)),
             float(mix(a.scale.z, b.scale.z))}};
}
TransformAnimationSource TransformAnimationSource::create(AssetId id) {
    auto key = animation_pose_json({});
    key["id"] = AssetId::generate();
    key["time_us"] = 0;
    key["interpolation"] = "linear";
    TransformAnimationSource result{
        {{"format", "forge.transform-animation"},
         {"version", 1},
         {"asset_id", id},
         {"timebase", animation_timebase},
         {"duration_us", 2 * animation_timebase},
         {"channels", {"forge.local_translation", "forge.local_rotation", "forge.local_scale"}},
         {"keys", nlohmann::json::array({key})}}};
    result.validate();
    return result;
}
TransformAnimationSource TransformAnimationSource::parse(std::span<const std::byte> bytes) {
    TransformAnimationSource result{
        asset_detail::parse_bounded_json(bytes, transform_animation_byte_limit, 500000, 12)};
    result.validate();
    return result;
}
AssetId TransformAnimationSource::asset() const { return document.at("asset_id").get<AssetId>(); }
TransformAnimationClip TransformAnimationSource::clip() const {
    require(document.at("format") == "forge.transform-animation" &&
                document.at("version").is_number_integer() &&
                (document.at("version") == 1 || document.at("version") == 2 ||
                 document.at("version") == 3 || document.at("version") == 4 ||
                 document.at("version") == 5) &&
                document.at("timebase") == animation_timebase,
            "Unsupported transform animation schema/timebase");
    TransformAnimationClip result;
    result.asset = asset();
    require(bool(result.asset), "Animation asset identity is empty");
    result.duration = time_value(document.at("duration_us"));
    require(result.duration > 0, "Animation duration must be positive");
    const auto& channels = document.at("channels");
    require(channels.is_array() && channels.size() <= 3, "Invalid animation channels");
    result.channels = 0;
    for (const auto& channel : channels) {
        const unsigned bit = channel == "forge.local_translation" ? 1
                             : channel == "forge.local_rotation"  ? 2
                             : channel == "forge.local_scale"     ? 4
                                                                  : 0;
        require(bit && !(result.channels & bit), "Unknown or duplicate animation channel binding");
        result.channels |= bit;
    }
    if (document.contains("properties")) {
        require(document.at("version") >= 3,
                "Animated properties require source version3 or newer");
        const auto& properties = document.at("properties");
        require(properties.is_array() && properties.size() <= animation_property_count,
                "Invalid animated property bindings");
        for (const auto& name : properties) {
            require(name.is_string(), "Animated property name must be a string");
            const auto property = animation_property_named(name.get_ref<const std::string&>());
            const auto bit = animation_property_bit(property);
            require(!(result.properties & bit), "Duplicate animated property binding");
            result.properties |= bit;
        }
    }
    if (document.at("version") == 5 && document.contains("material_bindings")) {
        const auto& bindings = document.at("material_bindings");
        require(bindings.is_array() && bindings.size() <= 64,
                "Animated material binding count exceeds64");
        std::set<std::pair<std::string, std::string>> identities;
        for (const auto& row : bindings) {
            auto binding = material_binding(row);
            require(identities.emplace(binding.slot, binding.key).second,
                    "Duplicate animated material slot/parameter binding");
            result.material_bindings.push_back(std::move(binding));
        }
    }
    require(result.channels || result.properties || !result.material_bindings.empty(),
            "Animation needs a transform channel or property");
    const auto& keys = document.at("keys");
    require(keys.is_array() && !keys.empty() && keys.size() <= 4096,
            "Animation requires 1–4096 keys");
    std::set<AssetId> ids;
    std::int64_t previous = -1;
    for (const auto& key : keys) {
        const auto id = key.at("id").get<AssetId>();
        const auto time = time_value(key.at("time_us"));
        const auto mode = key.at("interpolation");
        require(bool(id) && ids.insert(id).second, "Duplicate/empty animation key identity");
        require(time > previous && time <= result.duration,
                "Animation key times must be unique, sorted and within duration");
        require(mode == "linear" || mode == "held", "Unsupported animation interpolation");
        const auto pose = animation_pose(key);
        require(document.at("version") != 1 || !key.contains("curves"),
                "Vector curves require transform animation version2");
        AnimationPropertyValues values{};
        if (result.properties || key.contains("values")) {
            require(document.at("version") >= 3 && key.contains("values") &&
                        key.at("values").is_object(),
                    "Animated keys require version3 property values");
            const auto& fields = key.at("values");
            std::size_t expected = 0;
            for (const auto& binding : animation_property_bindings())
                if (result.properties & animation_property_bit(binding.property)) {
                    require(fields.contains(binding.name) && fields.at(binding.name).is_number(),
                            "Animated key is missing a numeric property value");
                    const auto value = fields.at(binding.name).get<double>();
                    validate_animation_property(binding.property, value);
                    values[unsigned(binding.property)] = value;
                    ++expected;
                }
            require(fields.size() == expected, "Animated key contains an undeclared property");
        }
        const auto settings = curves(key, document.at("version") >= 4);
        require(!(settings.property_mask & ~result.properties),
                "Property curve binds an undeclared property");
        auto materials = document.at("version") == 5 ? material_keys(key, result.material_bindings)
                                                     : std::vector<AnimationMaterialKey>{};
        result.keys.push_back(
            {id, time, pose, mode == "held", settings, values, std::move(materials)});
        previous = time;
    }
    validate_curve_segments(result);
    return result;
}
void TransformAnimationSource::validate() const {
    (void)clip();
    require(document.dump().size() <= transform_animation_byte_limit,
            "Animation source exceeds byte limit");
}
std::vector<std::byte> TransformAnimationSource::cook() const {
    validate();
    const auto source = document.dump();
    (void)parse(std::as_bytes(std::span(source)));
    const Json compiled{{"format", "forge.transform-clip"}, {"version", 5}, {"document", document}};
    const auto text = compiled.dump();
    const auto bytes = std::as_bytes(std::span(text));
    return {bytes.begin(), bytes.end()};
}
AssetId TransformAnimationSource::set_key(
    std::int64_t time, const LocalTransform& pose, bool held, AssetId id,
    std::optional<TransformAnimationCurves> curve,
    std::optional<AnimationPropertyValues> property_values,
    std::optional<std::span<const AnimationMaterialValues>> material_values) {
    const auto admitted = clip();
    auto candidate = *this;
    auto& keys = candidate.document.at("keys");
    if (!id)
        for (const auto& key : keys)
            if (key.at("time_us") == time)
                id = key.at("id").get<AssetId>();
    if (!id)
        id = AssetId::generate();
    auto key = Json::object();
    for (const auto& prior : keys)
        if (prior.at("id") == Json(id)) {
            key = prior;
            break;
        }
    const auto encoded_pose = animation_pose_json(pose);
    for (const auto& [field, value] : encoded_pose.items())
        key[field] = value;
    if (admitted.properties && (property_values || !key.contains("values"))) {
        const auto values = property_values.value_or(admitted.sample_properties(double(time)));
        auto& fields = key["values"];
        for (const auto& binding : animation_property_bindings())
            if (admitted.properties & animation_property_bit(binding.property))
                fields[binding.name] = values[unsigned(binding.property)];
    }
    if (!admitted.material_bindings.empty() &&
        (material_values || !key.contains("material_values"))) {
        std::vector<AnimationMaterialValues> sampled;
        if (!material_values) {
            sampled.resize(admitted.material_bindings.size());
            admitted.sample_materials(double(time), sampled);
            material_values = std::span<const AnimationMaterialValues>(sampled);
        }
        merge_material_values(key, admitted.material_bindings, *material_values);
    } else
        require(!material_values || material_values->empty(),
                "Material values require declared bindings");
    if (curve) {
        if (curve->property_mask)
            promote_property_curves(candidate.document);
        merge_curve_settings(key, *curve);
        candidate.document["version"] =
            std::max(curve->property_mask ? 4 : 2, candidate.document.at("version").get<int>());
    }
    key["id"] = id;
    key["time_us"] = time;
    key["interpolation"] = held ? "held" : "linear";
    std::erase_if(keys.get_ref<Json::array_t&>(),
                  [&](const auto& previous) { return previous.at("id") == Json(id); });
    keys.push_back(std::move(key));
    std::sort(keys.begin(), keys.end(),
              [](const auto& a, const auto& b) { return a.at("time_us") < b.at("time_us"); });
    candidate.validate();
    document.swap(candidate.document);
    return id;
}
void TransformAnimationSource::add_material_parameter(const AnimationMaterialBinding& binding,
                                                      const AnimationMaterialValues& initial) {
    const auto admitted = clip();
    const Json descriptor{
        {"slot", binding.slot}, {"key", binding.key}, {"type", unsigned(binding.type)}};
    (void)material_binding(descriptor);
    const auto encoded = material_numbers_json(binding, initial);
    for (const auto& prior : admitted.material_bindings)
        if (prior.slot == binding.slot && prior.key == binding.key) {
            require(prior.type == binding.type,
                    "Animated material binding cannot change type implicitly");
            return;
        }
    auto candidate = *this;
    promote_material_parameters(candidate.document);
    auto& bindings = candidate.document["material_bindings"];
    if (bindings.is_null())
        bindings = Json::array();
    bindings.push_back(descriptor);
    for (auto& key : candidate.document.at("keys")) {
        auto& rows = key["material_values"];
        if (rows.is_null())
            rows = Json::array();
        rows.push_back({{"slot", binding.slot}, {"key", binding.key}, {"value", encoded}});
    }
    candidate.validate();
    document.swap(candidate.document);
}
void TransformAnimationSource::erase_material_parameter(std::string_view slot,
                                                        std::string_view parameter) {
    const auto admitted = clip();
    const auto found = std::find_if(
        admitted.material_bindings.begin(), admitted.material_bindings.end(),
        [&](const auto& binding) { return binding.slot == slot && binding.key == parameter; });
    if (found == admitted.material_bindings.end())
        return;
    auto candidate = *this;
    std::erase_if(candidate.document.at("material_bindings").get_ref<Json::array_t&>(),
                  [&](const auto& row) { return same_material_binding(row, slot, parameter); });
    for (auto& key : candidate.document.at("keys"))
        std::erase_if(key.at("material_values").get_ref<Json::array_t&>(),
                      [&](const auto& row) { return same_material_binding(row, slot, parameter); });
    candidate.validate();
    document.swap(candidate.document);
}
void TransformAnimationSource::set_material_value(AssetId id, std::string_view slot,
                                                  std::string_view parameter,
                                                  const AnimationMaterialValues& values) {
    const auto admitted = clip();
    const auto index = material_binding_index(admitted, slot, parameter);
    const auto encoded = material_numbers_json(admitted.material_bindings[index], values);
    auto candidate = *this;
    bool changed = false;
    for (auto& key : candidate.document.at("keys"))
        if (key.at("id") == Json(id))
            for (auto& row : key.at("material_values"))
                if (same_material_binding(row, slot, parameter)) {
                    row["value"] = encoded;
                    changed = true;
                }
    require(changed, "Animated material key no longer exists");
    candidate.validate();
    document.swap(candidate.document);
}
void TransformAnimationSource::set_material_curve(AssetId id, std::string_view slot,
                                                  std::string_view parameter, unsigned lane,
                                                  const AnimationPropertyCurve& value) {
    set_material_curves(std::array{id}, slot, parameter, lane, value);
}
void TransformAnimationSource::add_property(AnimationProperty property, double initial_value) {
    const auto admitted = clip();
    validate_animation_property(property, initial_value);
    if (admitted.properties & animation_property_bit(property))
        return;
    auto candidate = *this;
    candidate.document["version"] = std::max(3, candidate.document.at("version").get<int>());
    const auto& binding = animation_property_binding(property);
    auto& list = candidate.document["properties"];
    if (list.is_null())
        list = Json::array();
    list.push_back(binding.name);
    for (auto& key : candidate.document.at("keys"))
        key["values"][binding.name] = initial_value;
    candidate.validate();
    document.swap(candidate.document);
}
void TransformAnimationSource::erase_property(AnimationProperty property) {
    const auto admitted = clip();
    const auto& binding = animation_property_binding(property);
    if (!(admitted.properties & animation_property_bit(property)))
        return;
    auto candidate = *this;
    std::erase(candidate.document.at("properties").get_ref<Json::array_t&>(), Json(binding.name));
    for (auto& key : candidate.document.at("keys")) {
        key.at("values").erase(binding.name);
        if (candidate.document.at("version") >= 4 && key.contains("curves") &&
            key["curves"].contains("property_curves"))
            key["curves"]["property_curves"].erase(binding.name);
    }
    candidate.validate();
    document.swap(candidate.document);
}
void TransformAnimationSource::set_property_curve(AssetId key, AnimationProperty property,
                                                  const AnimationPropertyCurve& value) {
    require(!animation_property_discrete(property),
            "Discrete animated properties do not support curve settings");
    const auto admitted = clip();
    const auto bit = animation_property_bit(animation_property_binding(property).property);
    require(admitted.properties & bit, "Property curve needs a declared binding");
    const auto found = std::find_if(admitted.keys.begin(), admitted.keys.end(),
                                    [&](const auto& entry) { return entry.id == key; });
    require(found != admitted.keys.end(), "Property curve key no longer exists");
    auto settings = found->curves;
    settings.property_mask |= bit;
    settings.properties[unsigned(property)] = value;
    auto candidate = *this;
    promote_property_curves(candidate.document);
    for (auto& entry : candidate.document.at("keys"))
        if (entry.at("id") == Json(key))
            merge_curve_settings(entry, settings);
    candidate.validate();
    document.swap(candidate.document);
}
void TransformAnimationSource::erase_key(AssetId id) {
    auto candidate = *this;
    auto& keys = candidate.document.at("keys").get_ref<Json::array_t&>();
    const auto count =
        std::erase_if(keys, [&](const auto& key) { return key.at("id") == Json(id); });
    require(count == 1, "Animation key no longer exists");
    candidate.validate();
    document.swap(candidate.document);
}
void TransformAnimationSource::move_key(AssetId id, std::int64_t time) {
    const auto checked = time_value(Json(time));
    for (const auto& key : document.at("keys"))
        if (key.at("id") == Json(id)) {
            move_keys(std::array<AssetId, 1>{id}, checked - time_value(key.at("time_us")));
            return;
        }
    throw std::runtime_error("Animation key no longer exists");
}
namespace {
std::set<AssetId> selected_animation_keys(const TransformAnimationSource& source,
                                          std::span<const AssetId> keys) {
    source.validate();
    require(!keys.empty() && keys.size() <= 4096, "Animation key selection is empty or too large");
    std::set<AssetId> selected;
    for (const auto identity : keys)
        require(bool(identity) && selected.insert(identity).second,
                "Animation key selection contains missing or duplicate identities");
    std::size_t found = 0;
    for (const auto& key : source.document.at("keys"))
        found += selected.contains(key.at("id").get<AssetId>());
    require(found == selected.size(), "Animation key selection no longer exists");
    return selected;
}
std::int64_t offset_animation_time(const Json& key, std::int64_t offset, std::int64_t duration) {
    const auto time = time_value(key.at("time_us"));
    require(offset >= -time && offset <= duration - time,
            "Animation key offset is outside the clip duration");
    return time + offset;
}
void sort_animation_keys(Json& keys) {
    std::sort(keys.begin(), keys.end(), [](const auto& left, const auto& right) {
        return left.at("time_us") < right.at("time_us");
    });
}
void merge_material_curve(Json& key, std::string_view slot, std::string_view parameter,
                          unsigned lane, unsigned width, const AnimationPropertyCurve& value) {
    for (auto& row : key.at("material_values"))
        if (same_material_binding(row, slot, parameter)) {
            auto& curves = row["curves"];
            if (curves.is_null()) {
                curves = Json::array();
                for (unsigned i = 0; i < width; ++i)
                    curves.push_back(
                        {{"interpolation", "linear"}, {"incoming", 0}, {"outgoing", 0}});
            }
            auto& fields = curves[lane];
            fields["interpolation"] = value.cubic ? "cubic" : "linear";
            fields["incoming"] = value.incoming;
            fields["outgoing"] = value.outgoing;
            return;
        }
    throw std::runtime_error("Animated material binding no longer exists");
}
} // namespace
void TransformAnimationSource::set_material_curves(std::span<const AssetId> keys,
                                                   std::string_view slot,
                                                   std::string_view parameter, unsigned lane,
                                                   const AnimationPropertyCurve& value) {
    const auto admitted = clip();
    const auto index = material_binding_index(admitted, slot, parameter);
    const auto width = material_parameter_width(admitted.material_bindings[index].type);
    require(lane < width, "Animated material curve lane exceeds parameter width");
    const auto selected = selected_animation_keys(*this, keys);
    auto candidate = *this;
    for (auto& key : candidate.document.at("keys"))
        if (selected.contains(key.at("id").get<AssetId>()))
            merge_material_curve(key, slot, parameter, lane, width, value);
    candidate.validate();
    document.swap(candidate.document);
}
void TransformAnimationSource::set_material_tangent_preset(std::span<const AssetId> keys,
                                                           std::string_view slot,
                                                           std::string_view parameter,
                                                           unsigned lane,
                                                           TransformAnimationTangentPreset preset) {
    require(preset == TransformAnimationTangentPreset::Flat ||
                preset == TransformAnimationTangentPreset::Linear ||
                preset == TransformAnimationTangentPreset::Smooth,
            "Unknown material tangent preset");
    const auto admitted = clip();
    const auto index = material_binding_index(admitted, slot, parameter);
    const auto width = material_parameter_width(admitted.material_bindings[index].type);
    require(lane < width, "Animated material curve lane exceeds parameter width");
    const auto selected = selected_animation_keys(*this, keys);
    auto candidate = *this;
    for (std::size_t i = 0; i < admitted.keys.size(); ++i) {
        const auto& key = admitted.keys[i];
        if (!selected.contains(key.id))
            continue;
        const double before =
            i ? double(key.time - admitted.keys[i - 1].time) / animation_timebase : 0;
        const double after = i + 1 < admitted.keys.size()
                                 ? double(admitted.keys[i + 1].time - key.time) / animation_timebase
                                 : 0;
        const auto value = key.materials[index].values[lane];
        const auto slopes = tangent_slopes(
            before ? (value - admitted.keys[i - 1].materials[index].values[lane]) / before : 0,
            after ? (admitted.keys[i + 1].materials[index].values[lane] - value) / after : 0,
            before, after, preset);
        merge_material_curve(candidate.document["keys"][i], slot, parameter, lane, width,
                             {true, slopes.first, slopes.second});
    }
    candidate.validate();
    document.swap(candidate.document);
}
void TransformAnimationSource::set_material_curves(
    AssetId id, std::span<const AnimationMaterialCurveEdit> values) {
    const auto admitted = clip();
    require(!values.empty() && values.size() <= 256,
            "Material curve edit set is empty or too large");
    const auto key = std::find_if(admitted.keys.begin(), admitted.keys.end(),
                                  [&](const auto& value) { return value.id == id; });
    require(key != admitted.keys.end(), "Animated material key no longer exists");
    std::set<std::tuple<std::string, std::string, unsigned>> changed;
    auto candidate = *this;
    auto& row = candidate.document.at("keys")[std::size_t(key - admitted.keys.begin())];
    for (const auto& value : values) {
        const auto& binding = value.binding;
        const auto index = material_binding_index(admitted, binding.slot, binding.key);
        require(admitted.material_bindings[index].type == binding.type,
                "Animated material curve semantic type changed");
        const auto width = material_parameter_width(binding.type);
        require(value.lane < width, "Animated material curve lane exceeds parameter width");
        require(changed.emplace(binding.slot, binding.key, value.lane).second,
                "Duplicate material curve edit");
        merge_material_curve(row, binding.slot, binding.key, value.lane, width, value.value);
    }
    candidate.validate();
    document.swap(candidate.document);
}
void TransformAnimationSource::move_keys(std::span<const AssetId> keys, std::int64_t offset) {
    const auto selected = selected_animation_keys(*this, keys);
    if (offset == 0)
        return;
    auto candidate = *this;
    const auto duration = time_value(document.at("duration_us"));
    for (auto& key : candidate.document.at("keys"))
        if (selected.contains(key.at("id").get<AssetId>()))
            key["time_us"] = offset_animation_time(key, offset, duration);
    sort_animation_keys(candidate.document.at("keys"));
    candidate.validate();
    document.swap(candidate.document);
}
void TransformAnimationSource::transform_key_values(std::span<const AssetId> keys,
                                                    Double3 position_multiplier,
                                                    Double3 position_offset,
                                                    Double3 scale_multiplier) {
    const auto selected = selected_animation_keys(*this, keys);
    bool identity = true;
    for (unsigned axis = 0; axis < 3; ++axis) {
        for (const auto value :
             {position_multiplier[axis], position_offset[axis], scale_multiplier[axis]})
            require(std::isfinite(value) && std::abs(value) <= 1e9,
                    "Animation value transform is nonfinite or out of range");
        identity &= position_multiplier[axis] == 1 && position_offset[axis] == 0 &&
                    scale_multiplier[axis] == 1;
    }
    if (identity)
        return;
    auto candidate = *this;
    for (auto& key : candidate.document.at("keys")) {
        if (!selected.contains(key.at("id").get<AssetId>()))
            continue;
        for (unsigned axis = 0; axis < 3; ++axis) {
            key["translation"][axis] =
                key["translation"][axis].get<double>() * position_multiplier[axis] +
                position_offset[axis];
            key["scale"][axis] = key["scale"][axis].get<double>() * scale_multiplier[axis];
            if (key.contains("curves"))
                for (const char* side : {"incoming", "outgoing"}) {
                    auto& slopes = key["curves"][side];
                    slopes["translation"][axis] =
                        slopes["translation"][axis].get<double>() * position_multiplier[axis];
                    slopes["scale"][axis] =
                        slopes["scale"][axis].get<double>() * scale_multiplier[axis];
                }
        }
    }
    candidate.validate();
    document.swap(candidate.document);
}

void TransformAnimationSource::retime_duration(std::int64_t new_duration) {
    validate();
    time_value(Json(new_duration));
    require(new_duration > 0, "Retimed animation duration must be positive");
    const auto old_duration = time_value(document.at("duration_us"));
    if (new_duration == old_duration)
        return;
    auto candidate = *this;
    candidate.document["duration_us"] = new_duration;
    const double slope_scale = double(old_duration) / double(new_duration);
    for (auto& key : candidate.document.at("keys")) {
        // Admitted times/durations <=3.6e9; even the rounded product fits uint64.
        const auto product =
            std::uint64_t(time_value(key.at("time_us"))) * std::uint64_t(new_duration);
        key["time_us"] =
            std::int64_t((product + std::uint64_t(old_duration) / 2) / std::uint64_t(old_duration));
        if (key.contains("curves")) {
            for (const char* side : {"incoming", "outgoing"})
                for (const char* channel : {"translation", "scale"})
                    for (auto& slope : key["curves"][side][channel])
                        slope = slope.get<double>() * slope_scale;
            if (candidate.document.at("version") >= 4 && key["curves"].contains("property_curves"))
                for (auto& settings : key["curves"]["property_curves"])
                    for (const auto* side : {"incoming", "outgoing"})
                        settings[side] = settings[side].get<double>() * slope_scale;
        }
        if (candidate.document.at("version") == 5 && key.contains("material_values"))
            for (auto& row : key.at("material_values"))
                if (row.contains("curves"))
                    for (auto& curve : row.at("curves"))
                        for (const auto* side : {"incoming", "outgoing"})
                            curve[side] = curve.at(side).get<double>() * slope_scale;
    }
    candidate.validate();
    document.swap(candidate.document);
}
void TransformAnimationSource::snap_keys_to_frames(std::span<const AssetId> keys, unsigned fps) {
    const auto selected = selected_animation_keys(*this, keys);
    require(fps >= 1 && fps <= 240, "Animation frame rate must be a whole number from 1 to 240");
    auto candidate = *this;
    for (auto& key : candidate.document.at("keys"))
        if (selected.contains(key.at("id").get<AssetId>())) {
            const auto time = std::uint64_t(time_value(key.at("time_us")));
            const auto frame = (time * fps + animation_timebase / 2) / animation_timebase;
            key["time_us"] = std::int64_t((frame * animation_timebase + fps / 2) / fps);
        }
    sort_animation_keys(candidate.document.at("keys"));
    candidate.validate();
    document.swap(candidate.document);
}
std::vector<AssetId> TransformAnimationSource::duplicate_keys(std::span<const AssetId> keys,
                                                              std::int64_t offset) {
    const auto selected = selected_animation_keys(*this, keys);
    require(document.at("keys").size() + selected.size() <= 4096,
            "Animation duplication exceeds the key limit");
    auto candidate = *this;
    const auto duration = time_value(document.at("duration_us"));
    std::vector<AssetId> identities;
    for (const auto& original : document.at("keys")) {
        if (!selected.contains(original.at("id").get<AssetId>()))
            continue;
        auto key = original;
        key["time_us"] = offset_animation_time(original, offset, duration);
        const auto identity = AssetId::generate();
        key["id"] = identity;
        identities.push_back(identity);
        candidate.document.at("keys").push_back(std::move(key));
    }
    sort_animation_keys(candidate.document.at("keys"));
    candidate.validate();
    document.swap(candidate.document);
    return identities;
}
void TransformAnimationSource::erase_keys(std::span<const AssetId> keys) {
    const auto selected = selected_animation_keys(*this, keys);
    require(selected.size() < document.at("keys").size(), "Animation must retain at least one key");
    auto candidate = *this;
    std::erase_if(candidate.document.at("keys").get_ref<Json::array_t&>(),
                  [&](const Json& key) { return selected.contains(key.at("id").get<AssetId>()); });
    candidate.validate();
    document.swap(candidate.document);
}
void TransformAnimationSource::set_tangent_preset(std::span<const AssetId> keys,
                                                  TransformAnimationTangentPreset preset) {
    require(preset == TransformAnimationTangentPreset::Flat ||
                preset == TransformAnimationTangentPreset::Linear ||
                preset == TransformAnimationTangentPreset::Smooth,
            "Unknown vector tangent preset");
    const auto selected = selected_animation_keys(*this, keys);
    const auto& original = document.at("keys");
    auto candidate = *this;
    candidate.document["version"] = std::max(2, candidate.document.at("version").get<int>());
    for (std::size_t i = 0; i < original.size(); ++i) {
        if (!selected.contains(original[i].at("id").get<AssetId>()))
            continue;
        const double before = i ? double(original[i].at("time_us").get<std::int64_t>() -
                                         original[i - 1].at("time_us").get<std::int64_t>()) /
                                      animation_timebase
                                : 0;
        const double after = i + 1 < original.size()
                                 ? double(original[i + 1].at("time_us").get<std::int64_t>() -
                                          original[i].at("time_us").get<std::int64_t>()) /
                                       animation_timebase
                                 : 0;
        TransformAnimationCurves settings{true};
        for (const char* field : {"translation", "scale"})
            for (unsigned axis = 0; axis < 3; ++axis) {
                const auto value = original[i].at(field)[axis].get<double>();
                double left =
                    before ? (value - original[i - 1].at(field)[axis].get<double>()) / before : 0;
                double right =
                    after ? (original[i + 1].at(field)[axis].get<double>() - value) / after : 0;
                const auto slopes = tangent_slopes(left, right, before, after, preset);
                left = slopes.first;
                right = slopes.second;
                auto& incoming = std::string_view(field) == "translation"
                                     ? settings.incoming.translation
                                     : settings.incoming.scale;
                auto& outgoing = std::string_view(field) == "translation"
                                     ? settings.outgoing.translation
                                     : settings.outgoing.scale;
                incoming[axis] = left;
                outgoing[axis] = right;
            }
        merge_curve_settings(candidate.document["keys"][i], settings);
    }
    candidate.validate();
    document.swap(candidate.document);
}
void TransformAnimationSource::set_property_tangent_preset(std::span<const AssetId> keys,
                                                           AnimationProperty property,
                                                           TransformAnimationTangentPreset preset) {
    require(!animation_property_discrete(property),
            "Discrete animated properties do not support tangents");
    require(preset == TransformAnimationTangentPreset::Flat ||
                preset == TransformAnimationTangentPreset::Linear ||
                preset == TransformAnimationTangentPreset::Smooth,
            "Unknown property tangent preset");
    const auto admitted = clip();
    const auto bit = animation_property_bit(animation_property_binding(property).property);
    require(admitted.properties & bit, "Property curve needs a declared binding");
    const auto selected = selected_animation_keys(*this, keys);
    auto candidate = *this;
    promote_property_curves(candidate.document);
    const auto index = unsigned(property);
    for (std::size_t i = 0; i < admitted.keys.size(); ++i) {
        const auto& key = admitted.keys[i];
        if (!selected.contains(key.id))
            continue;
        const double before =
            i ? double(key.time - admitted.keys[i - 1].time) / animation_timebase : 0;
        const double after = i + 1 < admitted.keys.size()
                                 ? double(admitted.keys[i + 1].time - key.time) / animation_timebase
                                 : 0;
        const auto slopes = tangent_slopes(
            before ? (key.properties[index] - admitted.keys[i - 1].properties[index]) / before : 0,
            after ? (admitted.keys[i + 1].properties[index] - key.properties[index]) / after : 0,
            before, after, preset);
        auto settings = key.curves;
        settings.property_mask |= bit;
        settings.properties[index] = {true, slopes.first, slopes.second};
        merge_curve_settings(candidate.document["keys"][i], settings);
    }
    candidate.validate();
    document.swap(candidate.document);
}
TransformAnimationClip decode_transform_animation(std::span<const std::byte> bytes, AssetId id) {
    auto doc =
        asset_detail::parse_bounded_json(bytes, transform_animation_cooked_byte_limit, 500016, 13);
    require(doc.at("format") == "forge.transform-clip", "Invalid cooked transform clip format");
    require(doc.at("version").is_number_integer(), "Cooked transform clip version must be integer");
    if (doc.at("version") == 4 || doc.at("version") == 5) {
        require(doc.at("version") == 5 || doc.at("document").at("version") <= 4,
                "Material animation source requires cooked version5");
        doc = doc.at("document").get<Json>();
    } else {
        require(doc.at("version") == 1 || doc.at("version") == 2 || doc.at("version") == 3,
                "Unsupported cooked transform clip version");
        doc["format"] = "forge.transform-animation";
    }
    const auto text = doc.dump();
    auto source = TransformAnimationSource::parse(std::as_bytes(std::span(text)));
    auto result = source.clip();
    require(result.asset == id, "Animation clip/asset identity mismatch");
    return result;
}
} // namespace forge
