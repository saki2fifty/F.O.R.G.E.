#include "animation_blend_space_geometry.hpp"
#include "bounded_json.hpp"
#include <algorithm>
#include <cmath>
#include <forge/animation_blend_space.hpp>
#include <set>
#include <tuple>
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
AnimationBlendSpaceSource AnimationBlendSpaceSource::create(AssetId id, unsigned dimensions) {
    require(bool(id), "Animation Blend Space needs an asset identity");
    require(dimensions == 1 || dimensions == 2, "Blend Space supports one or two axes");
    return {{{"format", "forge.animation-blend-space"},
             {"version", dimensions},
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
                (d.at("version") == 1 || d.at("version") == 2),
            "Unsupported Animation Blend Space format/version");
    require(bool(asset()), "Animation Blend Space asset identity is empty");
    if (!d.at("skeleton").is_null())
        require(bool(d.at("skeleton").get<AssetId>()), "Blend Space skeleton identity is empty");
    (void)number(d.at("cycle_seconds"), .0001, 3600,
                 "Blend Space cycle must be finite within0.0001–3600 seconds");
    const auto& rows = d.at("samples");
    require(rows.is_array() && rows.size() <= 64, "Blend Space supports at most64 samples");
    std::set<AssetId> ids;
    std::set<std::array<double, 2>> positions;
    for (const auto& row : rows) {
        const auto id = row.at("id").get<AssetId>();
        require(bool(id) && ids.insert(id).second,
                "Blend Space sample identity is empty or duplicated");
        std::array<double, 2> position{};
        if (d.at("version") == 1)
            position[0] = number(row.at("position"), -1000000, 1000000,
                                 "Blend Space position must be finite within±1000000");
        else {
            require(row.at("position").is_object(), "2D Blend Space position requires X and Y");
            position[0] = number(row.at("position").at("x"), -1000000, 1000000,
                                 "Blend Space X must be finite within±1000000");
            position[1] = number(row.at("position").at("y"), -1000000, 1000000,
                                 "Blend Space Y must be finite within±1000000");
        }
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
    result.dimensions = document.at("version").get<unsigned>();
    for (const auto& row : document.at("samples")) {
        require(!row.at("clip").is_null(), "Choose a clip for each Blend Space sample");
        const auto& position = row.at("position");
        result.samples.push_back(
            {row.at("id").get<AssetId>(),
             {row.at("clip").get<AssetId>()},
             result.dimensions == 1 ? position.get<double>() : position.at("x").get<double>(),
             result.dimensions == 1 ? 0 : position.at("y").get<double>()});
    }
    std::sort(result.samples.begin(), result.samples.end(), [](const auto& a, const auto& b) {
        return std::tie(a.position, a.position_y) < std::tie(b.position, b.position_y);
    });
    if (result.dimensions == 2)
        animation_detail::prepare_blend_geometry(result);
    return result;
}
AnimationBlendPair AnimationBlendSpace::select(double position) const {
    require(dimensions == 1, "2D Blend Space selection requires both X and Y");
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
AnimationBlendSelection AnimationBlendSpace::select(double x, double y) const {
    require(std::isfinite(x) && std::isfinite(y) && std::abs(x) <= 1000000 &&
                std::abs(y) <= 1000000 && !samples.empty(),
            "Blend Space selection requires finite coordinates and admitted samples");
    if (dimensions == 1) {
        const auto pair = select(x);
        return pair.first == pair.second
                   ? AnimationBlendSelection{{pair.first, 0, 0}, {1, 0, 0}, 1}
                   : AnimationBlendSelection{
                         {pair.first, pair.second, 0}, {1 - pair.weight, pair.weight, 0}, 2};
    }
    require(dimensions == 2 && points.size() == samples.size(), "Unprepared 2D Blend Space");
    return animation_detail::select_blend_geometry(*this, x, y);
}
std::size_t AnimationBlendSpace::resident_bytes() const {
    return sizeof(*this) + samples.capacity() * sizeof(AnimationBlendSample) +
           points.capacity() * sizeof(points.front()) +
           triangles.capacity() * sizeof(triangles.front()) +
           boundary.capacity() * sizeof(boundary.front());
}
std::vector<std::byte> AnimationBlendSpaceSource::cook(unsigned wrapper_version) const {
    if (!wrapper_version)
        wrapper_version = document.at("version").get<unsigned>();
    require((wrapper_version == 1 && document.at("version") == 1) || wrapper_version == 2,
            "Unsupported cooked Blend Space wrapper/source version");
    const auto space = compiled();
    auto canonical = document;
    canonical["samples"] = nlohmann::json::array();
    for (const auto& sample : space.samples) {
        const auto position = space.dimensions == 1 ? nlohmann::json(sample.position)
                                                    : nlohmann::json{{"x", sample.position},
                                                                     {"y", sample.position_y}};
        canonical["samples"].push_back(
            {{"id", sample.id}, {"clip", sample.clip.id}, {"position", position}});
    }
    const auto source = canonical.dump();
    (void)parse(std::as_bytes(std::span(source)));
    const auto text = nlohmann::json{
        {"format", "forge.animation-blend-space"},
        {"version", wrapper_version},
        {"document",
         canonical}}.dump();
    require(text.size() <= animation_blend_space_cooked_byte_limit,
            "Cooked Blend Space exceeds byte limit");
    const auto bytes = std::as_bytes(std::span(text));
    return {bytes.begin(), bytes.end()};
}
AnimationBlendSpaceSource AnimationBlendSpaceSource::parse_cooked(std::span<const std::byte> bytes,
                                                                  unsigned wrapper_version) {
    const auto cooked =
        asset_detail::parse_bounded_json(bytes, animation_blend_space_cooked_byte_limit, 12007, 13);
    require(cooked.at("format") == "forge.animation-blend-space" &&
                (cooked.at("version") == 1 || cooked.at("version") == 2) &&
                (cooked.at("version") == 2 || cooked.at("document").at("version") == 1),
            "Unsupported cooked Animation Blend Space format/version");
    require(!wrapper_version || cooked.at("version") == wrapper_version,
            "Animation Blend Space selected wrapper version differs");
    const auto text = cooked.at("document").dump();
    return parse(std::as_bytes(std::span(text)));
}
AnimationBlendSpace decode_animation_blend_space(std::span<const std::byte> bytes, AssetId id,
                                                 unsigned wrapper_version) {
    const auto source = AnimationBlendSpaceSource::parse_cooked(bytes, wrapper_version);
    require(bool(id) && source.asset() == id, "Cooked Blend Space identity mismatch");
    return source.compiled();
}
} // namespace forge
