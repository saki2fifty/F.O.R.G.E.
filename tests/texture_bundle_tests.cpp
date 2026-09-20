#include <forge/texture_bundle.hpp>
#include <iostream>
using namespace forge;
namespace {
void require(bool ok, const char* why) {
    if (!ok)
        throw std::runtime_error(why);
}
template <class F> void rejects(F fn) {
    try {
        fn();
    } catch (const std::exception&) {
        return;
    }
    throw std::runtime_error("Invalid texture bundle accepted");
}
} // namespace
int main() {
    try {
        TextureBundleIndex index{
            TextureSemantic::Color,
            {{TextureSemantic::Data, "texture-data.ftex", std::string(64, 'a'), 128},
             {TextureSemantic::Color, "texture-color.ftex", std::string(64, 'b'), 256}}};
        const auto bytes = encode_texture_bundle_index(index);
        auto read = decode_texture_bundle_index(bytes);
        require(read.find(TextureSemantic::Color).bytes == 256 &&
                    read.find(TextureSemantic::Data).bytes == 128,
                "Semantic variant selection changed");
        require(encode_texture_bundle_index(read) == bytes,
                "Texture bundle encoding not canonical");
        rejects([&] { read.find(TextureSemantic::Normal); });
        auto grouped = index;
        for (auto& v : grouped.variants)
            v.file = texture_variant_file(v.semantic, "image-7-");
        require(decode_texture_bundle_index(encode_texture_bundle_index(grouped))
                        .find(TextureSemantic::Data) == grouped.find(TextureSemantic::Data),
                "Shared model-artifact texture prefix lost");
        for (const auto* prefix : {"../", "a/", "a\\", "C:", ".", "UPPER"})
            rejects([&] { texture_variant_file(TextureSemantic::Color, prefix); });
        auto bad = index;
        bad.primary = TextureSemantic::Normal;
        rejects([&] { encode_texture_bundle_index(bad); });
        bad = index;
        bad.variants.push_back(bad.variants[0]);
        rejects([&] { encode_texture_bundle_index(bad); });
        bad = index;
        bad.variants[0].file = "../texture-data.ftex";
        rejects([&] { encode_texture_bundle_index(bad); });
        bad = index;
        bad.variants[0].digest[0] = 'z';
        rejects([&] { encode_texture_bundle_index(bad); });
        bad = index;
        bad.variants[0].bytes = 0;
        rejects([&] { encode_texture_bundle_index(bad); });
        bad = index;
        for (auto& e : bad.variants)
            e.bytes = 512ull * 1024 * 1024;
        rejects([&] { encode_texture_bundle_index(bad); });
        for (std::size_t i = 0; i < bytes.size(); ++i)
            rejects([&] { decode_texture_bundle_index(std::span(bytes).first(i)); });
        auto document = nlohmann::json::parse(bytes.begin(), bytes.end());
        auto reject_document = [&](const auto& j) {
            const auto text = j.dump();
            rejects([&] { decode_texture_bundle_index(std::as_bytes(std::span(text))); });
        };
        document["variants"][0]["semantic"] = 256;
        reject_document(document);
        document = nlohmann::json::parse(bytes.begin(), bytes.end());
        document["variants"][0]["bytes"] = -1;
        reject_document(document);
        const std::string duplicate =
            "{\"format\":\"forge.texture-bundle\",\"format\":\"different\"}";
        rejects([&] { decode_texture_bundle_index(std::as_bytes(std::span(duplicate))); });
        std::cout << "Cooked texture semantic index, bounds, names and rejection passed\n";
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
