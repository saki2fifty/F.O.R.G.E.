#include "asset_bytes.hpp"
#include "bounded_json.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <forge/transform_animation.hpp>
#include <set>
#include <string_view>
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
    document["version"] = 4;
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
    return sizeof(*this) + keys.capacity() * sizeof(TransformAnimationKey);
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
                 document.at("version") == 3 || document.at("version") == 4) &&
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
    require(result.channels || result.properties,
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
        result.keys.push_back({id, time, pose, mode == "held", settings, values});
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
    const Json compiled{{"format", "forge.transform-clip"}, {"version", 4}, {"document", document}};
    const auto text = compiled.dump();
    const auto bytes = std::as_bytes(std::span(text));
    return {bytes.begin(), bytes.end()};
}
AssetId TransformAnimationSource::set_key(std::int64_t time, const LocalTransform& pose, bool held,
                                          AssetId id, std::optional<TransformAnimationCurves> curve,
                                          std::optional<AnimationPropertyValues> property_values) {
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
} // namespace
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
    if (doc.at("version") == 4)
        doc = doc.at("document").get<Json>();
    else {
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
