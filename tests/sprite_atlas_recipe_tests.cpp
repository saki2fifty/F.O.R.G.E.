#include "sprite_atlas_document.hpp"
#include <iostream>
using namespace forge;
namespace {
void check(bool value, const char* why) {
    if (!value)
        throw std::runtime_error(why);
}
template <class F> void rejects(F&& action) {
    bool rejected = false;
    try {
        action();
    } catch (const std::exception&) {
        rejected = true;
    }
    check(rejected, "Invalid atlas source was accepted");
}
void identities_and_history() {
    const auto owner = AssetId::generate();
    const std::array images{AssetId::generate(), AssetId::generate(), AssetId::generate()};
    auto source = SpriteAtlasRecipe::create(owner, images);
    source.document["future"] = {{"retained", true}};
    source.document["frames"][1]["future"] = "keep row metadata";
    const auto seed = source;
    const auto bytes = source.document.dump(2);
    const auto restored = SpriteAtlasRecipe::parse(std::as_bytes(std::span(bytes)));
    check(restored.document == source.document && source.asset() == owner && source.next_id() == 3,
          "Atlas source roundtrip changed identity or opaque metadata");
    const std::array<std::uint32_t, 3> order{2, 0, 1};
    source.reorder(order);
    const auto reordered = source.inputs();
    check(reordered[0] == SpriteAtlasInput{2, images[2]} &&
              reordered[1] == SpriteAtlasInput{0, images[0]} &&
              reordered[2] == SpriteAtlasInput{1, images[1]} &&
              source.document["frames"][2]["future"] == "keep row metadata",
          "Reordering retargeted stable frames or lost row metadata");
    source.erase_image(0);
    const auto new_image = AssetId::generate();
    const auto added = source.add_image(new_image);
    check(added == 3 && source.next_id() == 4 &&
              source.inputs().back() == SpriteAtlasInput{3, new_image},
          "Removing a frame allowed its key to be silently reused");
    const auto replacement = AssetId::generate();
    source.replace_image(1, replacement);
    check(source.inputs()[1] == SpriteAtlasInput{1, replacement} &&
              source.document["future"] == seed.document["future"],
          "Replacing pixels changed frame identity or root metadata");
    const auto unchanged = source.document;
    rejects([&] { source.add_image(images[2]); });
    rejects([&] { source.replace_image(1, owner); });
    rejects([&] { source.erase_image(100); });
    const std::array<std::uint32_t, 3> repeated{2, 2, 3};
    rejects([&] { source.reorder(repeated); });
    check(source.document == unchanged, "Failed atlas mutation partially changed its source");
    auto undo = seed;
    SpriteAtlasDocumentTraits::restore_identity_metadata(undo, source);
    check(undo.next_id() == 4 && undo.inputs() == seed.inputs(),
          "History restoration lost reservation high-water state");
    check(undo.add_image(AssetId::generate()) == 4,
          "Undo reused a frame key allocated in later history");
    auto reused = source;
    reused.document["frames"].back()["id"] = 0;
    rejects([&] { SpriteAtlasDocumentTraits::validate_edit(source, reused); });
    auto backwards = source;
    backwards.document["next_id"] = 3;
    rejects([&] { SpriteAtlasDocumentTraits::validate_edit(source, backwards); });
}
void bounded_admission() {
    const auto owner = AssetId::generate(), image = AssetId::generate();
    const std::array images{image};
    auto source = SpriteAtlasRecipe::create(owner, images);
    const auto original = source.document;
    rejects([&] { source.erase_image(0); });
    check(source.document == original, "Last-frame rejection changed source");
    source.document["frames"][0]["id"] = UINT32_MAX;
    source.document["next_id"] = std::uint64_t(UINT32_MAX) + 1;
    source.validate();
    check(source.inputs()[0].id == UINT32_MAX, "Full uint32 frame key was truncated");
    rejects([&] { source.add_image(AssetId::generate()); });
    for (const nlohmann::json& value :
         {nlohmann::json(-1), nlohmann::json(1.5), nlohmann::json("0"), nlohmann::json(true)}) {
        auto invalid = source;
        invalid.document["frames"][0]["id"] = value;
        rejects([&] { invalid.validate(); });
    }
    rejects([&] { SpriteAtlasRecipe::create(owner, {}); });
    const std::array duplicate{image, image};
    rejects([&] { SpriteAtlasRecipe::create(owner, duplicate); });
    const std::array self{owner};
    rejects([&] { SpriteAtlasRecipe::create(owner, self); });
    std::vector<AssetId> excessive(513, image);
    rejects([&] { SpriteAtlasRecipe::create(owner, excessive); });
    rejects([&] {
        auto invalid = source;
        invalid.document["version"] = 2;
        invalid.validate();
    });
    rejects([&] {
        auto invalid = source;
        invalid.document["settings"]["padding"] = 65;
        invalid.validate();
    });
    rejects([&] {
        auto invalid = source;
        invalid.document["settings"]["extrude"] = 1;
        invalid.validate();
    });
    rejects([&] {
        auto invalid = source;
        invalid.document["frames"][0]["texture"] = AssetId{};
        invalid.validate();
    });
    rejects([&] {
        auto invalid = source;
        invalid.document["next_id"] = UINT32_MAX;
        invalid.validate();
    });
}
} // namespace
int main() {
    try {
        identities_and_history();
        bounded_admission();
        std::cout << "Atlas recipe: stable sparse frame keys, opaque data, strong edits and "
                     "history reservations PASS\n";
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
