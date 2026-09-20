#include "bounded_json.hpp"
#include <algorithm>
#include <forge/texture_bundle.hpp>
#include <set>
namespace forge {
std::string_view texture_variant_key(TextureSemantic semantic) {
    switch (semantic) {
    case TextureSemantic::Color:
        return "color";
    case TextureSemantic::Data:
        return "data";
    case TextureSemantic::Normal:
        return "normal";
    case TextureSemantic::HdrColor:
        return "hdr";
    }
    throw std::runtime_error("Unknown texture variant semantic");
}
std::string texture_variant_file(TextureSemantic semantic, std::string_view prefix) {
    if (prefix.size() > 64 || !std::all_of(prefix.begin(), prefix.end(), [](char c) {
            return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' || c == '_';
        }))
        throw std::runtime_error("Invalid texture bundle file prefix");
    return std::string(prefix) + "texture-" + std::string(texture_variant_key(semantic)) + ".ftex";
}
const TextureVariantEntry& TextureBundleIndex::find(TextureSemantic semantic) const {
    const auto found = std::find_if(variants.begin(), variants.end(),
                                    [&](const auto& x) { return x.semantic == semantic; });
    if (found == variants.end())
        throw std::runtime_error("Requested texture semantic variant was not cooked");
    return *found;
}
namespace {
void validate(const TextureBundleIndex& index) {
    (void)texture_variant_key(index.primary);
    if (index.variants.empty() || index.variants.size() > 4)
        throw std::runtime_error("Invalid texture variant count");
    std::set<TextureSemantic> kinds;
    std::uint64_t total = 0;
    for (const auto& entry : index.variants) {
        const auto suffix = texture_variant_file(entry.semantic);
        if (!entry.file.ends_with(suffix))
            throw std::runtime_error("Invalid texture variant suffix");
        const auto prefix =
            std::string_view(entry.file).substr(0, entry.file.size() - suffix.size());
        if (!kinds.insert(entry.semantic).second ||
            entry.file != texture_variant_file(entry.semantic, prefix) ||
            entry.digest.size() != 64 ||
            !std::all_of(entry.digest.begin(), entry.digest.end(),
                         [](char c) { return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'); }) ||
            entry.bytes < 24 || entry.bytes > 512ull * 1024 * 1024 ||
            entry.bytes > 512ull * 1024 * 1024 - total)
            throw std::runtime_error("Invalid texture variant name, digest or byte budget");
        total += entry.bytes;
    }
    (void)index.find(index.primary);
}
} // namespace
std::vector<std::byte> encode_texture_bundle_index(const TextureBundleIndex& index) {
    validate(index);
    nlohmann::json variants = nlohmann::json::array();
    auto ordered = index.variants;
    std::sort(ordered.begin(), ordered.end(),
              [](const auto& a, const auto& b) { return a.semantic < b.semantic; });
    for (const auto& e : ordered)
        variants.push_back({{"semantic", unsigned(e.semantic)},
                            {"file", e.file},
                            {"sha256", e.digest},
                            {"bytes", e.bytes}});
    const auto text = nlohmann::json{
        {"format", "forge.texture-bundle"},
        {"version", 1},
        {"primary", unsigned(index.primary)},
        {"variants",
         variants}}.dump();
    const auto bytes = std::as_bytes(std::span(text));
    return {bytes.begin(), bytes.end()};
}
TextureBundleIndex decode_texture_bundle_index(std::span<const std::byte> bytes) {
    const auto j = asset_detail::parse_bounded_json(bytes, 16384, 512, 8);
    auto number = [](const auto& v, std::uint64_t max) {
        if (!v.is_number_unsigned() || v.template get<std::uint64_t>() > max)
            throw std::runtime_error("Invalid texture bundle integer");
        return v.template get<std::uint64_t>();
    };
    if (j.at("format") != "forge.texture-bundle" || number(j.at("version"), 1) != 1 ||
        !j.at("variants").is_array() || j.at("variants").size() > 4)
        throw std::runtime_error("Unsupported texture bundle index");
    TextureBundleIndex result;
    result.primary = TextureSemantic(number(j.at("primary"), 3));
    for (const auto& e : j.at("variants"))
        result.variants.push_back({TextureSemantic(number(e.at("semantic"), 3)),
                                   e.at("file").template get<std::string>(),
                                   e.at("sha256").template get<std::string>(),
                                   number(e.at("bytes"), 512ull * 1024 * 1024)});
    validate(result);
    return result;
}
} // namespace forge
