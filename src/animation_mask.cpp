#include "bounded_json.hpp"
#include <algorithm>
#include <cmath>
#include <forge/animation_mask.hpp>
#include <map>
#include <set>
namespace forge {
namespace {
void require(bool value, const char* message) {
    if (!value)
        throw std::runtime_error(message);
}
float weight(const nlohmann::json& value) {
    require(value.is_number(), "Animation Mask influence must be numeric");
    const auto number = value.get<double>();
    require(std::isfinite(number) && number >= 0 && number <= 1,
            "Animation Mask influence must be finite within0–1");
    return float(number);
}
} // namespace
AnimationMaskSource AnimationMaskSource::create(AssetId id) {
    require(bool(id), "Animation Mask needs an asset identity");
    return {{{"format", "forge.animation-mask"},
             {"version", 1},
             {"asset_id", id},
             {"skeleton", nullptr},
             {"default_weight", 0},
             {"entries", nlohmann::json::array()}}};
}
AnimationMaskSource AnimationMaskSource::parse(std::span<const std::byte> bytes) {
    AnimationMaskSource result{
        asset_detail::parse_bounded_json(bytes, animation_mask_byte_limit, 12000, 12)};
    result.validate();
    return result;
}
AssetId AnimationMaskSource::asset() const { return document.at("asset_id").get<AssetId>(); }
void AnimationMaskSource::validate() const {
    const auto& d = document;
    require(d.is_object() && d.at("format") == "forge.animation-mask" && d.at("version") == 1,
            "Unsupported Animation Mask format/version");
    require(bool(asset()), "Animation Mask asset identity is empty");
    if (!d.at("skeleton").is_null())
        require(bool(d.at("skeleton").get<AssetId>()), "Animation Mask skeleton identity is empty");
    (void)weight(d.at("default_weight"));
    const auto& entries = d.at("entries");
    require(entries.is_array() && entries.size() <= 1024,
            "Animation Mask supports at most1024 joint entries");
    std::set<AssetId> selected;
    for (const auto& entry : entries) {
        require(entry.is_object(), "Animation Mask entry must be an object");
        const auto joint = entry.at("joint").get<AssetId>();
        require(bool(joint) && selected.insert(joint).second,
                "Animation Mask joint identity is empty or duplicated");
        (void)weight(entry.at("weight"));
        require(entry.at("descendants").is_boolean(),
                "Animation Mask descendants setting must be boolean");
    }
}
AnimationMask AnimationMaskSource::compiled() const {
    validate();
    require(!document.at("skeleton").is_null(),
            "Choose an imported skeleton for the Animation Mask");
    AnimationMask result{
        asset(), document.at("skeleton").get<AssetId>(), weight(document.at("default_weight")), {}};
    for (const auto& entry : document.at("entries"))
        result.entries.push_back({entry.at("joint").get<AssetId>(), weight(entry.at("weight")),
                                  entry.at("descendants").get<bool>()});
    std::sort(result.entries.begin(), result.entries.end(),
              [](const auto& a, const auto& b) { return a.joint < b.joint; });
    return result;
}
std::size_t AnimationMask::resident_bytes() const {
    return sizeof(*this) + entries.capacity() * sizeof(AnimationMaskEntry);
}
std::vector<float> AnimationMask::weights(AssetId expected, std::span<const AssetId> joints,
                                          std::span<const std::int16_t> parents) const {
    require(bool(skeleton) && expected == skeleton, "Animation Mask uses a different skeleton");
    require(!joints.empty() && joints.size() <= 1024 && joints.size() == parents.size(),
            "Animation Mask requires durable imported joint identities");
    require(std::isfinite(default_weight) && default_weight >= 0 && default_weight <= 1,
            "Animation Mask default influence is invalid");
    std::map<AssetId, std::size_t> indices;
    for (std::size_t i = 0; i < joints.size(); ++i) {
        require(bool(joints[i]) && indices.emplace(joints[i], i).second,
                "Animation Mask skeleton joint identities are missing or duplicated");
        require(parents[i] == -1 || (parents[i] >= 0 && std::size_t(parents[i]) < i),
                "Animation Mask skeleton ancestry is invalid");
    }
    std::vector<const AnimationMaskEntry*> selected(joints.size(), nullptr);
    require(entries.size() <= 1024, "Animation Mask entry count exceeds limit");
    for (const auto& entry : entries) {
        const auto found = indices.find(entry.joint);
        require(found != indices.end(),
                "Animation Mask joint is missing or belongs to another skeleton");
        require(!selected[found->second] && std::isfinite(entry.weight) && entry.weight >= 0 &&
                    entry.weight <= 1,
                "Animation Mask entry is duplicated or has invalid influence");
        selected[found->second] = &entry;
    }
    std::vector<float> result(joints.size()), inherited(joints.size());
    for (std::size_t i = 0; i < joints.size(); ++i) {
        const auto base = parents[i] < 0 ? default_weight : inherited[std::size_t(parents[i])];
        const auto* entry = selected[i];
        result[i] = entry ? entry->weight : base;
        inherited[i] = entry && entry->descendants ? entry->weight : base;
    }
    return result;
}
std::vector<std::byte> AnimationMaskSource::cook() const {
    const auto mask = compiled();
    auto canonical = document;
    canonical["entries"] = nlohmann::json::array();
    for (const auto& entry : mask.entries)
        canonical["entries"].push_back(
            {{"joint", entry.joint}, {"weight", entry.weight}, {"descendants", entry.descendants}});
    const auto source = canonical.dump();
    (void)parse(std::as_bytes(std::span(source)));
    const auto text =
        nlohmann::json{{"format", "forge.animation-mask"}, {"version", 1}, {"document", canonical}}
            .dump();
    require(text.size() <= animation_mask_cooked_byte_limit,
            "Cooked Animation Mask exceeds byte limit");
    const auto bytes = std::as_bytes(std::span(text));
    return {bytes.begin(), bytes.end()};
}
AnimationMaskSource AnimationMaskSource::parse_cooked(std::span<const std::byte> bytes) {
    auto cooked =
        asset_detail::parse_bounded_json(bytes, animation_mask_cooked_byte_limit, 12007, 13);
    require(cooked.at("format") == "forge.animation-mask" && cooked.at("version") == 1,
            "Unsupported cooked Animation Mask format/version");
    const auto text = cooked.at("document").dump();
    return parse(std::as_bytes(std::span(text)));
}
AnimationMask decode_animation_mask(std::span<const std::byte> bytes, AssetId asset) {
    const auto source = AnimationMaskSource::parse_cooked(bytes);
    require(bool(asset) && source.asset() == asset, "Cooked Animation Mask identity mismatch");
    return source.compiled();
}
} // namespace forge
