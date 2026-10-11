#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <map>
#include <set>
namespace {
void animation_key_batch_tests() {
    auto source = TransformAnimationSource::create(AssetId::generate());
    source.document["duration_us"] = 6 * animation_timebase;
    const auto first = source.document.at("keys")[0].at("id").get<AssetId>();
    TransformAnimationCurves curve;
    curve.cubic = true;
    source.set_key(0, {}, false, first, curve);
    const auto second =
        source.set_key(animation_timebase, {{2, 0, 0}, {}, {1, 1, 1}}, true, {}, curve);
    const auto last = source.set_key(4 * animation_timebase, {{4, 0, 0}, {}, {1, 1, 1}});
    source.document["extension"] = {{"batch", 42}};
    for (auto& key : source.document.at("keys"))
        key["extension"] = {{"original", true}, {"data", {1, 2, 3}}};
    source.validate();
    const auto original = source.document;
    const std::array selected{first, second};
    auto moved = source, reversed = source;
    moved.move_keys(selected, animation_timebase);
    reversed.move_keys(std::array{second, first}, animation_timebase);
    require(moved.document == reversed.document, "Selection order changed batch movement");
    std::map<AssetId, Json> expected;
    for (const auto& key : original.at("keys")) {
        auto copy = key;
        const auto id = copy.at("id").get<AssetId>();
        if (id == first || id == second)
            copy["time_us"] = copy.at("time_us").get<std::int64_t>() + animation_timebase;
        expected.emplace(id, copy);
    }
    for (const auto& key : moved.document.at("keys"))
        require(key == expected.at(key.at("id").get<AssetId>()),
                "Batch move changed complete pose/held/curves/extensions/identity");
    require(moved.document.at("extension") == original.at("extension"),
            "Batch move changed source extension");
    for (unsigned i = 0; i <= 100; ++i) {
        const double u = double(i) / 100;
        const auto pose = moved.clip().sample((1 + u) * animation_timebase);
        require(std::abs(pose.translation.x - (6 * u * u - 4 * u * u * u)) < 1e-6,
                "Translated zero-slope cubic differs from independent analytic motion");
    }
    const auto once = moved.document;
    moved.move_keys(selected, 0);
    require(moved.document == once, "Zero batch offset changed source");
    auto single = source;
    single.move_key(first, 500000);
    auto batch = source;
    batch.move_keys(std::array{first}, 500000);
    require(single.document == batch.document, "Legacy retiming lost complete key metadata");
    for (const auto& ids : std::vector<std::vector<AssetId>>{
             {}, {first, first}, {AssetId{}}, {AssetId::generate()}}) {
        auto bad = source;
        rejects([&] { bad.move_keys(ids, 0); });
        require(bad.document == original, "Invalid selection changed source");
    }
    for (auto offset : std::array<std::int64_t, 4>{std::numeric_limits<std::int64_t>::min(),
                                                   std::numeric_limits<std::int64_t>::max(), -1,
                                                   4 * animation_timebase}) {
        auto bad = source;
        rejects([&] { bad.move_keys(selected, offset); });
        require(bad.document == original, "Rejected offset/collision changed source");
    }
    auto copied = source;
    const auto fresh = copied.duplicate_keys(std::array{last, second}, animation_timebase);
    require(fresh.size() == 2 && fresh[0] != fresh[1], "Duplicate identities missing or repeated");
    const std::set<AssetId> originals{first, second, last};
    for (const auto id : fresh)
        require(bool(id) && !originals.contains(id), "Duplicate reused a source key identity");
    const std::array ordered{second, last};
    for (unsigned i = 0; i < fresh.size(); ++i) {
        const auto old =
            std::find_if(original.at("keys").begin(), original.at("keys").end(),
                         [&](const Json& key) { return key.at("id") == Json(ordered[i]); });
        auto copy = *old;
        copy["id"] = fresh[i];
        copy["time_us"] = copy.at("time_us").get<std::int64_t>() + animation_timebase;
        const auto at =
            std::find_if(copied.document.at("keys").begin(), copied.document.at("keys").end(),
                         [&](const Json& key) { return key.at("id") == Json(fresh[i]); });
        require(at != copied.document.at("keys").end() && *at == copy,
                "Duplicate changed full source metadata or canonical return order");
    }
    copied.erase_keys(fresh);
    require(copied.document == original, "Deleting copies changed original keys");
    const std::array all{first, second, last};
    rejects([&] { copied.erase_keys(all); });
    require(copied.document == original, "All-key deletion changed source");
    rejects([&] { copied.duplicate_keys(selected, 0); });
    require(copied.document == original, "Duplicate collision changed source");
    auto bulky = source;
    bulky.document["extension"]["payload"] = std::string(600000, 'x');
    for (auto& key : bulky.document.at("keys"))
        key["extension"]["payload"] = std::string(400000, 'y');
    bulky.validate();
    const auto bounded = bulky.document;
    rejects([&] { bulky.duplicate_keys(std::array{second, last}, animation_timebase); });
    require(bulky.document == bounded, "Duplication byte-budget failure changed source");
    auto large = TransformAnimationSource::create(AssetId::generate());
    large.document["duration_us"] = 10000;
    const auto prototype = large.document.at("keys")[0];
    large.document["keys"] = Json::array();
    std::vector<AssetId> ids;
    for (unsigned i = 0; i < 4096; ++i) {
        auto key = prototype;
        const auto id = AssetId::generate();
        key["id"] = id;
        key["time_us"] = i;
        large.document["keys"].push_back(key);
        ids.push_back(id);
    }
    large.validate();
    const auto before = large.document;
    large.move_keys(ids, 1);
    require(large.document.at("keys").front().at("time_us") == 1 &&
                large.document.at("keys").back().at("time_us") == 4096,
            "4096-key atomic overlapping shift differs");
    require(decode_transform_animation(large.cook(), large.asset()).keys.size() == 4096,
            "Batch move cook lost keys");
    const auto full = large.document;
    rejects([&] { large.duplicate_keys(std::array{ids[0]}, 5000); });
    require(large.document == full, "Key-limit rejection changed source");
    large.move_keys(ids, -1);
    require(large.document == before, "4096-key shift roundtrip changed source");
}
} // namespace
