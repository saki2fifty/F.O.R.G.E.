#include "sprite_atlas_recipe.hpp"
#include "bounded_json.hpp"
#include "sprite_atlas_settings.hpp"
#include <algorithm>
#include <map>
#include <set>

namespace forge {
namespace {
using Json = nlohmann::json;
std::uint64_t number(const Json& value, std::uint64_t maximum) {
    if (!value.is_number_integer() ||
        (!value.is_number_unsigned() && value.get<std::int64_t>() < 0) ||
        value.get<std::uint64_t>() > maximum)
        throw std::invalid_argument("Atlas frame key or sequence is outside its integer range");
    return value.get<std::uint64_t>();
}
Json& frame(Json& document, std::uint32_t id) {
    for (auto& row : document.at("frames"))
        if (number(row.at("id"), UINT32_MAX) == id)
            return row;
    throw std::invalid_argument("Atlas frame key is missing");
}
void adopt(SpriteAtlasRecipe& value, SpriteAtlasRecipe candidate) {
    candidate.validate();
    value.document.swap(candidate.document);
}
} // namespace
SpriteAtlasRecipe SpriteAtlasRecipe::create(AssetId id, std::span<const AssetId> textures,
                                            SpriteAtlasPacking packing) {
    if (!id || textures.empty() || textures.size() > sprite_frame_region_limit)
        throw std::invalid_argument("Atlas creation needs an identity and 1 to 512 Texture refs");
    Json frames = Json::array();
    for (const auto texture : textures)
        frames.push_back({{"id", frames.size()}, {"texture", texture}});
    SpriteAtlasRecipe result{{{"format", "forge.sprite-atlas"},
                              {"version", 1},
                              {"asset_id", id},
                              {"next_id", textures.size()},
                              {"settings", asset_detail::sprite_atlas_settings(packing)},
                              {"frames", std::move(frames)}}};
    result.validate();
    return result;
}
SpriteAtlasRecipe SpriteAtlasRecipe::parse(std::span<const std::byte> bytes) {
    SpriteAtlasRecipe result{
        asset_detail::parse_bounded_json(bytes, sprite_atlas_recipe_byte_limit, 20000, 16)};
    result.validate();
    return result;
}
AssetId SpriteAtlasRecipe::asset() const {
    const auto result = document.at("asset_id").get<AssetId>();
    if (!result)
        throw std::invalid_argument("Atlas source needs a persistent Texture AssetId");
    return result;
}
std::uint64_t SpriteAtlasRecipe::next_id() const {
    return number(document.at("next_id"), std::uint64_t(UINT32_MAX) + 1);
}
std::vector<SpriteAtlasInput> SpriteAtlasRecipe::inputs() const {
    const auto owner = asset();
    const auto next = next_id();
    const auto& rows = document.at("frames");
    if (!rows.is_array() || rows.empty() || rows.size() > sprite_frame_region_limit)
        throw std::invalid_argument("Atlas source needs 1 to 512 image frames");
    std::vector<SpriteAtlasInput> result;
    std::set<std::uint32_t> ids;
    std::set<AssetId> textures;
    result.reserve(rows.size());
    for (const auto& row : rows) {
        SpriteAtlasInput input{std::uint32_t(number(row.at("id"), UINT32_MAX)),
                               row.at("texture").get<AssetId>()};
        if (!input.texture || input.texture == owner || input.id >= next ||
            !ids.insert(input.id).second || !textures.insert(input.texture).second)
            throw std::invalid_argument(
                "Atlas frames need distinct reserved keys and independent Texture refs");
        result.push_back(input);
    }
    return result;
}
SpriteAtlasPacking SpriteAtlasRecipe::settings() const {
    return asset_detail::sprite_atlas_settings(document.at("settings"));
}
void SpriteAtlasRecipe::validate() const {
    if (!document.is_object() || document.at("format") != "forge.sprite-atlas" ||
        number(document.at("version"), 1) != 1)
        throw std::invalid_argument("Unsupported Sprite atlas source");
    (void)inputs();
    (void)settings();
}
std::uint32_t SpriteAtlasRecipe::add_image(AssetId texture) {
    validate();
    const auto next = next_id();
    if (next > UINT32_MAX)
        throw std::invalid_argument("Atlas frame key sequence is exhausted");
    auto candidate = *this;
    candidate.document["frames"].push_back({{"id", next}, {"texture", texture}});
    candidate.document["next_id"] = next + 1;
    adopt(*this, std::move(candidate));
    return std::uint32_t(next);
}
void SpriteAtlasRecipe::replace_image(std::uint32_t id, AssetId texture) {
    validate();
    auto candidate = *this;
    frame(candidate.document, id)["texture"] = texture;
    adopt(*this, std::move(candidate));
}
void SpriteAtlasRecipe::erase_image(std::uint32_t id) {
    validate();
    auto candidate = *this;
    auto& rows = candidate.document.at("frames");
    const auto found = std::find_if(rows.begin(), rows.end(), [&](const auto& row) {
        return number(row.at("id"), UINT32_MAX) == id;
    });
    if (found == rows.end())
        throw std::invalid_argument("Atlas frame key is missing");
    rows.erase(found);
    adopt(*this, std::move(candidate));
}
void SpriteAtlasRecipe::reorder(std::span<const std::uint32_t> order) {
    validate();
    if (order.size() != document.at("frames").size())
        throw std::invalid_argument("Atlas order must contain every existing frame key once");
    std::map<std::uint32_t, Json> previous;
    for (const auto& row : document.at("frames"))
        previous.emplace(std::uint32_t(number(row.at("id"), UINT32_MAX)), row);
    auto candidate = *this;
    candidate.document["frames"] = Json::array();
    for (const auto id : order) {
        const auto found = previous.find(id);
        if (found == previous.end())
            throw std::invalid_argument("Atlas order repeats or omits a frame key");
        candidate.document["frames"].push_back(std::move(found->second));
        previous.erase(found);
    }
    adopt(*this, std::move(candidate));
}
std::vector<SpriteFrameEntry> sprite_atlas_frame_entries(const SpriteAtlasRecipe& recipe,
                                                         const SpriteAtlasLayout& layout) {
    const auto inputs = recipe.inputs();
    if (inputs.size() != layout.frames.size())
        throw std::invalid_argument("Atlas frame layout does not match recipe inputs");
    std::vector<SpriteFrameEntry> result;
    result.reserve(inputs.size());
    for (std::size_t i = 0; i < inputs.size(); ++i)
        result.push_back({inputs[i].id, layout.frames[i]});
    std::sort(result.begin(), result.end(),
              [](const auto& a, const auto& b) { return a.id < b.id; });
    return result;
}
} // namespace forge
