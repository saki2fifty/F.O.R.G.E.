#include "asset_bytes.hpp"
#include "bounded_json.hpp"
#include <algorithm>
#include <cmath>
#include <forge/transform_animation.hpp>
#include <set>
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
    double a, b, c, d;
    double at(double t) const { return ((a * t + b) * t + c) * t + d; }
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
    return {2 * p0 - 2 * p1 + left + right, -3 * p0 + 3 * p1 - 2 * left - right, left, p0};
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
TransformAnimationCurves curves(const Json& key) {
    if (!key.contains("curves"))
        return {};
    const auto& object = key.at("curves");
    const auto mode = object.at("interpolation");
    require(mode == "linear" || mode == "cubic", "Invalid vector curve interpolation");
    return {mode == "cubic", tangent(object, "incoming"), tangent(object, "outgoing")};
}
void validate_curve_segments(const TransformAnimationClip& clip) {
    for (std::size_t i = 1; i < clip.keys.size(); ++i) {
        const auto& left = clip.keys[i - 1];
        const auto& right = clip.keys[i];
        if (left.held || !left.curves.cubic)
            continue;
        const auto seconds = double(right.time - left.time) / animation_timebase;
        for (unsigned axis = 0; axis < 3; ++axis) {
            if (clip.channels & 1) {
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
            if (clip.channels & 4) {
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
                (document.at("version") == 1 || document.at("version") == 2) &&
                document.at("timebase") == animation_timebase,
            "Unsupported transform animation schema/timebase");
    TransformAnimationClip result;
    result.asset = asset();
    require(bool(result.asset), "Animation asset identity is empty");
    result.duration = time_value(document.at("duration_us"));
    require(result.duration > 0, "Animation duration must be positive");
    const auto& channels = document.at("channels");
    require(channels.is_array() && !channels.empty() && channels.size() <= 3,
            "Invalid animation channels");
    result.channels = 0;
    for (const auto& channel : channels) {
        const unsigned bit = channel == "forge.local_translation" ? 1
                             : channel == "forge.local_rotation"  ? 2
                             : channel == "forge.local_scale"     ? 4
                                                                  : 0;
        require(bit && !(result.channels & bit), "Unknown or duplicate animation channel binding");
        result.channels |= bit;
    }
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
        require(document.at("version") == 2 || !key.contains("curves"),
                "Vector curves require transform animation version2");
        result.keys.push_back({id, time, pose, mode == "held", curves(key)});
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
    auto compiled = document;
    compiled["format"] = "forge.transform-clip";
    compiled["version"] = 2;
    const auto text = compiled.dump();
    const auto bytes = std::as_bytes(std::span(text));
    return {bytes.begin(), bytes.end()};
}
AssetId TransformAnimationSource::set_key(std::int64_t time, const LocalTransform& pose, bool held,
                                          AssetId id,
                                          std::optional<TransformAnimationCurves> curve) {
    auto candidate = *this;
    auto& keys = candidate.document.at("keys");
    if (!id)
        for (const auto& key : keys)
            if (key.at("time_us") == time)
                id = key.at("id").get<AssetId>();
    if (!id)
        id = AssetId::generate();
    auto key = animation_pose_json(pose);
    if (curve) {
        key["curves"] = curves_json(*curve);
        candidate.document["version"] = 2;
    } else {
        for (const auto& prior : keys)
            if (prior.at("id") == Json(id) && prior.contains("curves"))
                key["curves"] = prior.at("curves");
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
    for (const auto& key : document.at("keys"))
        if (key.at("id") == Json(id)) {
            set_key(time, animation_pose(key), key.at("interpolation") == "held", id);
            return;
        }
    throw std::runtime_error("Animation key no longer exists");
}
TransformAnimationClip decode_transform_animation(std::span<const std::byte> bytes, AssetId id) {
    auto doc = asset_detail::parse_bounded_json(bytes, transform_animation_byte_limit, 500000, 12);
    require(doc.at("format") == "forge.transform-clip", "Invalid cooked transform clip format");
    doc["format"] = "forge.transform-animation";
    TransformAnimationSource source{std::move(doc)};
    auto result = source.clip();
    require(result.asset == id, "Animation clip/asset identity mismatch");
    return result;
}
} // namespace forge
