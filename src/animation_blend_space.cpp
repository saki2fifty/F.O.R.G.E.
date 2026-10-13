#include "bounded_json.hpp"
#include <algorithm>
#include <cmath>
#include <forge/animation_blend_space.hpp>
#include <set>
namespace forge {
namespace {
void require(bool value, const char* why) {
    if (!value)
        throw std::runtime_error(why);
}
double number(const nlohmann::json& value, double low, double high, const char* why) {
    require(value.is_number(), why);
    const double result = value.get<double>();
    require(std::isfinite(result) && result >= low && result <= high, why);
    return result;
}
} // namespace
AnimationBlendSpaceSource AnimationBlendSpaceSource::create(AssetId id) {
    require(bool(id), "Animation Blend Space needs an asset identity");
    return {{{"format", "forge.animation-blend-space"},
             {"version", 1},
             {"asset_id", id},
             {"skeleton", nullptr},
             {"cycle_seconds", 1},
             {"samples", nlohmann::json::array()}}};
}
AnimationBlendSpaceSource AnimationBlendSpaceSource::parse(std::span<const std::byte> bytes) {
    AnimationBlendSpaceSource result{
        asset_detail::parse_bounded_json(bytes, animation_blend_space_byte_limit, 12000, 12)};
    result.validate();
    return result;
}
AssetId AnimationBlendSpaceSource::asset() const { return document.at("asset_id").get<AssetId>(); }
void AnimationBlendSpaceSource::validate() const {
    const auto& d = document;
    require(d.is_object() && d.at("format") == "forge.animation-blend-space" &&
                d.at("version") == 1,
            "Unsupported Animation Blend Space format/version");
    require(bool(asset()), "Animation Blend Space asset identity is empty");
    if (!d.at("skeleton").is_null())
        require(bool(d.at("skeleton").get<AssetId>()), "Blend Space skeleton identity is empty");
    (void)number(d.at("cycle_seconds"), .0001, 3600,
                 "Blend Space cycle must be finite within0.0001–3600 seconds");
    const auto& rows = d.at("samples");
    require(rows.is_array() && rows.size() <= 64, "Blend Space supports at most64 samples");
    std::set<AssetId> ids;
    std::set<double> positions;
    for (const auto& row : rows) {
        const auto id = row.at("id").get<AssetId>();
        require(bool(id) && ids.insert(id).second,
                "Blend Space sample identity is empty or duplicated");
        const auto position = number(row.at("position"), -1000000, 1000000,
                                     "Blend Space position must be finite within±1000000");
        require(positions.insert(position).second, "Blend Space sample positions must be unique");
        if (!row.at("clip").is_null())
            require(bool(row.at("clip").get<AssetId>()), "Blend Space clip identity is empty");
    }
}
AnimationBlendSpace AnimationBlendSpaceSource::compiled() const {
    validate();
    require(!document.at("skeleton").is_null(), "Choose an imported skeleton for the Blend Space");
    require(!document.at("samples").empty(), "Blend Space needs at least one animation sample");
    AnimationBlendSpace result{asset(),
                               document.at("skeleton").get<AssetId>(),
                               document.at("cycle_seconds").get<double>(),
                               {}};
    for (const auto& row : document.at("samples")) {
        require(!row.at("clip").is_null(), "Choose a clip for each Blend Space sample");
        result.samples.push_back({row.at("id").get<AssetId>(),
                                  {row.at("clip").get<AssetId>()},
                                  row.at("position").get<double>()});
    }
    std::sort(result.samples.begin(), result.samples.end(),
              [](const auto& a, const auto& b) { return a.position < b.position; });
    return result;
}
AnimationBlendPair AnimationBlendSpace::select(double position) const {
    require(std::isfinite(position) && std::abs(position) <= 1000000 && !samples.empty(),
            "Blend Space selection requires a finite position and admitted samples");
    const auto found =
        std::lower_bound(samples.begin(), samples.end(), position,
                         [](const auto& sample, double p) { return sample.position < p; });
    if (found == samples.end())
        return {samples.size() - 1, samples.size() - 1, 0};
    const auto index = std::size_t(found - samples.begin());
    if (index == 0 || found->position == position)
        return {index, index, 0};
    return {index - 1, index,
            (position - samples[index - 1].position) /
                (samples[index].position - samples[index - 1].position)};
}
std::size_t AnimationBlendSpace::resident_bytes() const {
    return sizeof(*this) + samples.capacity() * sizeof(AnimationBlendSample);
}
std::vector<std::byte> AnimationBlendSpaceSource::cook() const {
    const auto space = compiled();
    auto canonical = document;
    canonical["samples"] = nlohmann::json::array();
    for (const auto& sample : space.samples)
        canonical["samples"].push_back(
            {{"id", sample.id}, {"clip", sample.clip.id}, {"position", sample.position}});
    const auto source = canonical.dump();
    (void)parse(std::as_bytes(std::span(source)));
    const auto text = nlohmann::json{
        {"format", "forge.animation-blend-space"},
        {"version", 1},
        {"document",
         canonical}}.dump();
    require(text.size() <= animation_blend_space_cooked_byte_limit,
            "Cooked Blend Space exceeds byte limit");
    const auto bytes = std::as_bytes(std::span(text));
    return {bytes.begin(), bytes.end()};
}
AnimationBlendSpaceSource
AnimationBlendSpaceSource::parse_cooked(std::span<const std::byte> bytes) {
    const auto cooked =
        asset_detail::parse_bounded_json(bytes, animation_blend_space_cooked_byte_limit, 12007, 13);
    require(cooked.at("format") == "forge.animation-blend-space" && cooked.at("version") == 1,
            "Unsupported cooked Animation Blend Space format/version");
    const auto text = cooked.at("document").dump();
    return parse(std::as_bytes(std::span(text)));
}
AnimationBlendSpace decode_animation_blend_space(std::span<const std::byte> bytes, AssetId id) {
    const auto source = AnimationBlendSpaceSource::parse_cooked(bytes);
    require(bool(id) && source.asset() == id, "Cooked Blend Space identity mismatch");
    return source.compiled();
}
} // namespace forge
