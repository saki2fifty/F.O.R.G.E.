#include "animation_retarget.hpp"
#include "bounded_json.hpp"
#include <map>
#include <set>

namespace forge {
namespace {
void require(bool value, const char* message) {
    if (!value)
        throw std::runtime_error(message);
}
void settings_valid(const AnimationRetargetSettings& value) {
    require(value.sampling_rate >= 1 && value.sampling_rate <= 120,
            "Retarget sampling rate must be within 1 to 120");
    require(std::isfinite(value.translation_scale) && value.translation_scale >= 0 &&
                value.translation_scale <= 100,
            "Retarget translation scale must be finite within 0 to 100");
    double length = 0;
    for (const auto lane : value.alignment) {
        require(std::isfinite(lane), "Retarget alignment must be a finite unit quaternion");
        length += lane * lane;
    }
    require(std::abs(length - 1) <= 1e-4, "Retarget alignment must be a finite unit quaternion");
    require(value.entries.size() <= animation_retarget_joint_limit,
            "Retarget mapping exceeds 256 joint pairs");
    std::set<AssetId> sources, targets;
    for (const auto& entry : value.entries)
        require(entry.source && entry.target && sources.insert(entry.source).second &&
                    targets.insert(entry.target).second,
                "Retarget joint pairs must have unique nonempty source and target identities");
}
} // namespace
bool is_animation_retarget_source(const AssetRecord& record) {
    return record.type == AnimationClipAsset::type && !record.subasset &&
           (path_utf8(record.source).ends_with(".retarget.json") ||
            record.metadata.contains("forge.animation_clip"));
}
AnimationRetargetSource AnimationRetargetSource::create(AssetId id) {
    require(bool(id), "Retarget source requires an asset identity");
    return {{{"format", "forge.animation-retarget"},
             {"version", 1},
             {"asset_id", id},
             {"source_skeleton", nullptr},
             {"source_clip", nullptr},
             {"target_skeleton", nullptr},
             {"sampling_rate", 30},
             {"root_translation", false},
             {"translation_scale", 1},
             {"alignment", {0, 0, 0, 1}},
             {"entries", nlohmann::json::array()}}};
}
AnimationRetargetSource AnimationRetargetSource::parse(std::span<const std::byte> bytes) {
    AnimationRetargetSource result{
        asset_detail::parse_bounded_json(bytes, animation_retarget_byte_limit, 8000, 12)};
    result.validate();
    return result;
}
AssetId AnimationRetargetSource::asset() const { return document.at("asset_id").get<AssetId>(); }
void AnimationRetargetSource::validate() const {
    const auto& d = document;
    require(d.is_object() && d.at("format") == "forge.animation-retarget" && d.at("version") == 1,
            "Unsupported animation retarget format/version");
    require(bool(asset()), "Retarget source identity is empty");
    for (const auto* key : {"source_skeleton", "source_clip", "target_skeleton"})
        if (!d.at(key).is_null())
            require(bool(d.at(key).get<AssetId>()), "Retarget asset reference is empty");
    require(d.at("sampling_rate").is_number_integer() && d.at("sampling_rate") >= 1 &&
                d.at("sampling_rate") <= 120 && d.at("root_translation").is_boolean() &&
                d.at("translation_scale").is_number() && d.at("alignment").is_array() &&
                d.at("alignment").size() == 4 && d.at("entries").is_array(),
            "Invalid retarget settings");
    AnimationRetargetSettings value;
    value.sampling_rate = d.at("sampling_rate").get<unsigned>();
    value.root_translation = d.at("root_translation").get<bool>();
    value.translation_scale = d.at("translation_scale").get<double>();
    value.alignment = d.at("alignment").get<std::array<double, 4>>();
    require(d.at("entries").size() <= animation_retarget_joint_limit,
            "Retarget mapping exceeds 256 joint pairs");
    for (const auto& entry : d.at("entries"))
        value.entries.push_back(
            {entry.at("source").get<AssetId>(), entry.at("target").get<AssetId>()});
    settings_valid(value);
}
AnimationRetargetSettings AnimationRetargetSource::settings() const {
    validate();
    AnimationRetargetSettings value;
    for (const auto* key : {"source_skeleton", "source_clip", "target_skeleton"})
        require(!document.at(key).is_null(),
                "Choose source Skeleton/Clip and target Skeleton before publishing");
    value.source_skeleton = document.at("source_skeleton").get<AssetId>();
    value.source_clip = document.at("source_clip").get<AssetId>();
    value.target_skeleton = document.at("target_skeleton").get<AssetId>();
    value.sampling_rate = document.at("sampling_rate").get<unsigned>();
    value.root_translation = document.at("root_translation").get<bool>();
    value.translation_scale = document.at("translation_scale").get<double>();
    value.alignment = document.at("alignment").get<std::array<double, 4>>();
    for (const auto& entry : document.at("entries"))
        value.entries.push_back(
            {entry.at("source").get<AssetId>(), entry.at("target").get<AssetId>()});
    require(!value.entries.empty(), "Map at least one source joint to a target joint");
    return value;
}
namespace animation_detail {
unsigned retarget_sample_intervals(float duration, unsigned sampling_rate,
                                   std::size_t source_joints, std::size_t target_joints) {
    require(std::isfinite(duration) && duration >= .0001f && duration <= 60 && sampling_rate >= 1 &&
                sampling_rate <= 120 && source_joints && target_joints &&
                source_joints <= animation_retarget_joint_limit &&
                target_joints <= animation_retarget_joint_limit,
            "Retarget duration, sampling rate or joint counts exceed the supported profile");
    const auto intervals = std::max(1u, unsigned(std::ceil(double(duration) * sampling_rate)));
    require(intervals + 1 <= 4096 && intervals + 1 <= animation_retarget_sample_limit /
                                                          std::max(source_joints, target_joints),
            "Retarget sampled-joint budget exceeded; reduce sampling rate or clip duration");
    return intervals;
}
namespace {
RootRigid orientation(const LocalPose& value) {
    for (const auto lane : value.translation)
        require(std::isfinite(lane) && std::abs(lane) <= 1e6,
                "Retarget pose translation exceeds its finite coordinate bounds");
    for (const auto lane : value.scale)
        require(std::isfinite(lane) && std::abs(lane - 1) <= 1e-5,
                "Retargeting requires unit joint scales");
    RootRigid result;
    double length = 0;
    for (const auto lane : value.rotation)
        length += double(lane) * lane;
    require(std::isfinite(length) && std::abs(length - 1) <= 1e-3,
            "Retarget pose rotation must be a finite unit quaternion");
    std::copy(value.rotation.begin(), value.rotation.end(), result.rotation.begin());
    return root_checked(result);
}
std::vector<RootRigid> rig_rest(const RetargetRig& rig) {
    require(!rig.joints.empty() && rig.joints.size() <= animation_retarget_joint_limit &&
                rig.rest.size() == rig.joints.size() && rig.parents.size() == rig.joints.size(),
            "Retarget skeleton needs 1 to 256 durable joints with rest transforms and parents");
    std::set<AssetId> ids;
    std::vector<RootRigid> models;
    models.reserve(rig.joints.size());
    for (std::size_t i = 0; i < rig.joints.size(); ++i) {
        require(rig.joints[i] && ids.insert(rig.joints[i]).second,
                "Retarget skeleton joint identities are empty or duplicated");
        const auto parent = rig.parents[i];
        require(parent == -1 || (parent >= 0 && std::size_t(parent) < i),
                "Retarget skeleton parents must precede children");
        const auto local = orientation(rig.rest[i]);
        models.push_back(parent < 0 ? local : root_compose(models[std::size_t(parent)], local));
    }
    return models;
}
} // namespace
RetargetPose::RetargetPose(RetargetRig source, RetargetRig target,
                           const AnimationRetargetSettings& settings)
    : source_(std::move(source)), target_(std::move(target)), source_rest_(rig_rest(source_)),
      target_rest_(rig_rest(target_)), mapping_(target_.joints.size(), -1),
      translation_scale_(settings.translation_scale) {
    settings_valid(settings);
    require(!settings.entries.empty(), "Retarget mapping is empty");
    alignment_ = root_checked({{}, settings.alignment});
    std::map<AssetId, int> source_indices, target_indices;
    for (std::size_t i = 0; i < source_.joints.size(); ++i)
        source_indices.emplace(source_.joints[i], int(i));
    for (std::size_t i = 0; i < target_.joints.size(); ++i)
        target_indices.emplace(target_.joints[i], int(i));
    unsigned roots = 0;
    for (const auto& entry : settings.entries) {
        require(source_indices.contains(entry.source) && target_indices.contains(entry.target),
                "Retarget joint is missing or belongs to another skeleton");
        const auto from = source_indices.at(entry.source), to = target_indices.at(entry.target);
        mapping_[std::size_t(to)] = from;
        if (target_.parents[std::size_t(to)] < 0) {
            require(source_.parents[std::size_t(from)] < 0,
                    "Retarget roots must map to source roots");
            ++roots;
            if (settings.root_translation)
                translation_joint_ = to;
        }
    }
    for (std::size_t to = 0; to < mapping_.size(); ++to) {
        const auto from = mapping_[to];
        const auto parent = target_.parents[to];
        if (from < 0 || parent < 0)
            continue;
        require(mapping_[std::size_t(parent)] >= 0 &&
                    source_.parents[std::size_t(from)] == mapping_[std::size_t(parent)],
                "Retarget mapped chains require matching direct parent mappings");
    }
    require(!settings.root_translation || roots == 1,
            "Root translation requires exactly one mapped root pair");
}
std::vector<LocalPose> RetargetPose::sample(std::span<const LocalPose> source,
                                            std::stop_token stop) const {
    require(source.size() == source_.rest.size(), "Retarget sampled source joint count differs");
    std::vector<RootRigid> source_models;
    source_models.reserve(source.size());
    for (std::size_t i = 0; i < source.size(); ++i) {
        if (stop.stop_requested())
            throw std::runtime_error("Animation retargeting cancelled");
        const auto local = orientation(source[i]);
        source_models.push_back(
            source_.parents[i] < 0
                ? local
                : root_compose(source_models[std::size_t(source_.parents[i])], local));
    }
    auto result = target_.rest;
    std::vector<RootRigid> target_models;
    target_models.reserve(result.size());
    const auto inverse_alignment = root_inverse(alignment_);
    for (std::size_t to = 0; to < result.size(); ++to) {
        const auto parent = target_.parents[to];
        const auto parent_model = parent < 0 ? RootRigid{} : target_models[std::size_t(parent)];
        RootRigid model;
        if (const auto from = mapping_[to]; from >= 0) {
            const auto delta = root_compose(source_models[std::size_t(from)],
                                            root_inverse(source_rest_[std::size_t(from)]));
            model = root_compose(root_compose(root_compose(alignment_, delta), inverse_alignment),
                                 target_rest_[to]);
            const auto local = root_compose(root_inverse(parent_model), model);
            std::copy(local.rotation.begin(), local.rotation.end(), result[to].rotation.begin());
            if (int(to) == translation_joint_) {
                std::array<double, 3> displacement;
                for (unsigned axis = 0; axis < 3; ++axis)
                    displacement[axis] = double(source[std::size_t(from)].translation[axis]) -
                                         source_.rest[std::size_t(from)].translation[axis];
                displacement = root_rotate(alignment_.rotation, displacement);
                for (unsigned axis = 0; axis < 3; ++axis)
                    result[to].translation[axis] += float(displacement[axis] * translation_scale_);
            }
        } else
            model = root_compose(parent_model, orientation(result[to]));
        (void)orientation(result[to]);
        target_models.push_back(model);
    }
    return result;
}
} // namespace animation_detail
} // namespace forge
