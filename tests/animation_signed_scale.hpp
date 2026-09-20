#pragma once
// Uses the real official converter, FORGE admission, Ozz SamplingJob and
// LocalToModelJob through the existing runtime consumer. No GPU claim here.
void signed_scale_animation(const std::filesystem::path& root,
                            const std::filesystem::path& converter, const Json& source) {
    for (const char* interpolation : {"LINEAR", "STEP", "CUBICSPLINE"}) {
        const auto folder = root / interpolation;
        std::filesystem::create_directories(folder / "Assets");
        auto doc = source;
        doc["nodes"][0]["scale"] = {-1, 1, 1};
        doc["nodes"][1]["translation"] = {1, 2, 3};
        doc["animations"][0]["channels"][0]["target"]["path"] = "scale";
        doc["animations"][0]["samplers"][0]["interpolation"] = interpolation;
        std::vector<float> values{0, 1};
        if (std::string(interpolation) == "CUBICSPLINE") {
            // Equal -4 tangents make this Hermite segment exactly linear.
            for (float value : {-4.f, 0.f, 0.f, 2.f, 1.f, 1.f, -4.f, 0.f, 0.f, -4.f, 0.f, 0.f, -2.f,
                                1.f, 1.f, -4.f, 0.f, 0.f})
                values.push_back(value);
        } else
            for (float value : {2.f, 1.f, 1.f, -2.f, 1.f, 1.f})
                values.push_back(value);
        doc["buffers"][0] = {{"uri", "scale.bin"}, {"byteLength", values.size() * 4}};
        doc["bufferViews"][1]["byteLength"] = (values.size() - 2) * 4;
        doc["accessors"][1]["count"] = (values.size() - 2) / 3;
        {
            std::ofstream bytes(folder / "Assets/scale.bin", std::ios::binary);
            for (float value : values) {
                const auto bits = std::bit_cast<std::uint32_t>(value);
                for (unsigned i = 0; i < 4; ++i)
                    bytes.put(static_cast<char>((bits >> (8 * i)) & 255));
            }
            std::ofstream file(folder / "Assets/scale.gltf");
            file << doc.dump();
        }
        const auto records =
            prepare_animation_conversion(folder, "Assets/scale.gltf", converter).publish();
        AssetId skeleton, clip;
        for (const auto& record : records) {
            if (record.type == SkeletonAsset::type)
                skeleton = record.id;
            if (record.type == AnimationClipAsset::type)
                clip = record.id;
        }
        check(bool(skeleton) && bool(clip), "Signed animation conversion lost outputs");
        Json config = {{"skeleton", skeleton},  {"clip", clip},  {"enabled", true},
                       {"play_on_start", true}, {"loop", false}, {"playback_speed", 1}};
        Json scene = {
            {"version", 1},
            {"entities", Json::array({{{"id", "actor"},
                                       {"name", "Actor"},
                                       {"components",
                                        {{"forge.position", {{"x", 0}, {"y", 0}, {"z", 0}}},
                                         {"forge.animator", config}}}}})}};
        Fixture runtime(folder);
        runtime.load(scene);
        const auto authored = runtime.scene.snapshot();
        auto verify = [&](double expected) {
            auto pose = runtime.pose();
            check(!pose.is_null() && pose.at("model").size() == 2, "Signed Ozz pose unavailable");
            for (const auto& matrix : pose.at("model"))
                for (const auto& element : matrix)
                    check(std::isfinite(element.get<double>()),
                          "Signed Ozz model matrix is not finite");
            check(std::abs(pose["model"][0][0].get<double>() + 1) < .002,
                  "Ozz lost reflected parent");
            check(std::abs(pose["model"][1][0].get<double>() - expected) < .002,
                  "Ozz signed/zero sampling mismatch");
            check(std::abs(pose["model"][1][12].get<double>() + 1) < .002,
                  "Ozz hierarchy did not reflect child translation");
        };
        verify(-2);
        runtime.simulation.tick(.5f);
        verify(std::string(interpolation) == "STEP" ? -2 : 0);
        runtime.simulation.tick(.5f);
        verify(2);
        check(runtime.scene.snapshot() == authored, "Signed sampling mutated authored scene");
    }
}
