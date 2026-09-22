#pragma once
template <class Fixture>
void check_animation_pointers(const Fixture& linear, const Fixture& step, const Fixture& cubic,
                              const Fixture& morph) {
    auto point = [](Fixture& fixture) {
        fixture.doc["extensionsUsed"] = {"KHR_animation_pointer"};
        for (auto& channel : fixture.doc["animations"][0]["channels"]) {
            auto& target = channel["target"];
            const auto node = target.at("node").template get<unsigned>();
            const auto path = target.at("path").template get<std::string>();
            target = {{"path", "pointer"},
                      {"extensions",
                       {{"KHR_animation_pointer",
                         {{"pointer", "/nodes/" + std::to_string(node) + "/" + path}}}}}};
        }
    };
    for (auto fixture : {linear, step, cubic, morph}) {
        const auto core = fixture.result();
        point(fixture);
        const auto original = fixture.doc["animations"];
        const auto native = fixture.native();
        const auto clip = native->animation(0);
        require(clip.duration == core.duration && clip.tracks.size() == core.tracks.size() &&
                    clip.tracks[0].node == core.tracks[0].node &&
                    clip.tracks[0].path == core.tracks[0].path &&
                    clip.tracks[0].interpolation == core.tracks[0].interpolation &&
                    *clip.tracks[0].values == *core.tracks[0].values &&
                    native->source().document["animations"] == original,
                "Pointer/core animation differs or source target was rewritten");
    }
    auto pointer = linear;
    point(pointer);
    for (const auto* path :
         {"/nodes/00/translation", "/nodes/-1/translation", "/nodes/+0/translation",
          "/nodes/999999999999999999999/translation", "/nodes/1/translation", "/nodes/0",
          "/nodes/0/translation/0", "/nodes/0/translation~1x", "/materials/0/alphaCutoff",
          "#/nodes/0/scale"}) {
        auto bad = pointer;
        bad.doc["animations"][0]["channels"][0]["target"]["extensions"]["KHR_animation_pointer"]
               ["pointer"] = path;
        rejects([&] { bad.result(); });
    }
    auto bad = pointer;
    bad.doc["animations"][0]["channels"][0]["target"]["node"] = 0;
    rejects([&] { bad.result(); }, "no target.node");
    bad = pointer;
    bad.doc["animations"][0]["channels"][0]["target"]["path"] = "translation";
    rejects([&] { bad.result(); }, "path=pointer");
    bad = pointer;
    bad.doc["animations"][0]["channels"][0]["target"].erase("extensions");
    rejects([&] { bad.result(); }, "payload");
    bad = pointer;
    bad.doc["animations"][0]["channels"].push_back(linear.doc["animations"][0]["channels"][0]);
    rejects([&] { bad.result(); }, "duplicate node/path");
    bad = pointer;
    bad.doc["nodes"][0]["matrix"] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
    rejects([&] { bad.result(); }, "matrix-authored");
    // Pointer float properties permit normalized AND unnormalized integers,
    // including translation. Core translation remains FLOAT-only.
    for (const bool normalized : {false, true}) {
        auto numeric = pointer;
        auto& accessor = numeric.doc["accessors"][1];
        accessor["componentType"] = 5121;
        accessor["normalized"] = normalized;
        numeric.doc["bufferViews"][1]["byteLength"] = 6;
        const std::array<unsigned, 6> values{0, 127, 255, 255, 0, 127};
        for (unsigned i = 0; i < values.size(); ++i)
            numeric.bytes[8 + i] = std::byte(values[i]);
        const auto actual = numeric.result();
        require((*actual.tracks[0].values)[2] == (normalized ? 1.f : 255.f),
                "Pointer integer output conversion differs from its object-model float type");
        numeric.doc["animations"][0]["channels"][0]["target"] = {{"node", 0},
                                                                 {"path", "translation"}};
        rejects([&] { numeric.result(); }, "output format");
    }
}
