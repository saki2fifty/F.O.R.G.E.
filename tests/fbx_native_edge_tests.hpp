#pragma once
namespace {
void replace_fbx_text(std::string& source, const std::string& before, const std::string& after) {
    std::size_t at = 0;
    while ((at = source.find(before, at)) != std::string::npos) {
        source.replace(at, before.size(), after);
        at += after.size();
    }
}
void check_fbx_edges() {
    auto material_source = test::fbx_textured_triangle();
    replace_fbx_text(material_source, " C: \"OP\",4001,4000,\"DiffuseColor\"\n", "");
    replace_fbx_text(
        material_source, "  P: \"DiffuseFactor\"",
        "  P: \"EmissiveColor\", \"Color\", \"\", \"A\",0.1,0.2,0.3\n  P: \"DiffuseFactor\"");
    auto material = convert_fbx_interchange(std::as_bytes(std::span(material_source)), {},
                                            nlohmann::json::object());
    const auto& values = material.generated.document.at("materials")[0];
    check(!values.contains("alphaMode") &&
              values.at("pbrMetallicRoughness").at("baseColorFactor")[3] == 1 &&
              values.at("pbrMetallicRoughness").at("roughnessFactor") == 1,
          "Unspecified FBX material values did not use opaque diffuse defaults");
    check(values.at("emissiveFactor") == nlohmann::json::array({.1, .2, .3}),
          "FBX emissive color without an explicit factor disappeared");
    replace_fbx_text(
        material_source, "  P: \"DiffuseFactor\"",
        "  P: \"TransparentColor\", \"Color\", \"\", \"A\",1,1,1\n"
        "  P: \"TransparencyFactor\", \"Number\", \"\", \"A\",1\n  P: \"DiffuseFactor\"");
    material = convert_fbx_interchange(std::as_bytes(std::span(material_source)), {},
                                       nlohmann::json::object());
    const auto& transparent = material.generated.document.at("materials")[0];
    check(transparent.at("alphaMode") == "BLEND" &&
              transparent.at("pbrMetallicRoughness").at("baseColorFactor")[3] == 0,
          "Explicit transparent FBX material was replaced by an opaque default");
    replace_fbx_text(material_source, "\"TransparentColor\", \"Color\", \"\", \"A\",1,1,1",
                     "\"TransparentColor\", \"Color\", \"\", \"A\",0.1,0.2,0.3");
    rejects(
        [&] {
            (void)convert_fbx_interchange(std::as_bytes(std::span(material_source)), {},
                                          nlohmann::json::object());
        },
        "FBX colored transparency was silently reduced to scalar opacity");
    auto source = test::fbx_triangle();
    replace_fbx_text(source, "1001", "9007199254740993");
    replace_fbx_text(source, "1002", "9007199254740995");
    auto prepared =
        convert_fbx_interchange(std::as_bytes(std::span(source)), {}, nlohmann::json::object());
    check(prepared.evidence.at("/meshes/0").exporter_key == "fbx:mesh:9007199254740993",
          "FBX 64-bit source UID passed through lossy float storage");
    check(std::any_of(prepared.evidence.begin(), prepared.evidence.end(),
                      [](const auto& item) {
                          return item.second.exporter_key == "fbx:node:9007199254740995";
                      }),
          "FBX large node UID was not retained exactly");
    source = test::fbx_triangle();
    replace_fbx_text(source, "FBXVersion: 7400", "FBXVersion: 9999");
    rejects([&] { (void)inspect_fbx(std::as_bytes(std::span(source))); },
            "Unknown FBX source version was accepted");
    source = test::fbx_triangle();
    replace_fbx_text(source, "a:0,0,0,1,0,0,0,1,0", "a:0,0,0,1e100,0,0,0,1,0");
    rejects(
        [&] {
            (void)convert_fbx_interchange(std::as_bytes(std::span(source)), {},
                                          nlohmann::json::object());
        },
        "FBX geometry overflowed float storage without rejection");
    source = test::fbx_triangle();
    replace_fbx_text(
        source, "  P: \"Lcl Scaling\"",
        "  P: \"GeometricTranslation\", \"Vector3D\", \"\", \"A\",5,0,0\n  P: \"Lcl Scaling\"");
    prepared =
        convert_fbx_interchange(std::as_bytes(std::span(source)), {}, nlohmann::json::object());
    bool helper = false;
    for (const auto& [address, evidence] : prepared.evidence)
        if (evidence.exporter_key == "fbx:node:1002:geometry") {
            const auto node = std::stoul(address.substr(7));
            check(prepared.generated.document.at("nodes")[node].at("translation")[0] == 5,
                  "FBX geometry-only transform was not preserved in its helper");
            helper = true;
        }
    check(helper, "FBX geometry helper lost its stable owner role");
    NativeGltfDocument geometry(std::move(prepared.generated));
    check(!cook_static_gltf_bundle(geometry).empty(),
          "FBX geometric transform failed shared cooking");
    source = test::fbx_triangle();
    replace_fbx_text(source, "\"UpAxis\", \"int\", \"Integer\", \"\",1",
                     "\"UpAxis\", \"int\", \"Integer\", \"\",2");
    replace_fbx_text(source, "\"FrontAxis\", \"int\", \"Integer\", \"\",2",
                     "\"FrontAxis\", \"int\", \"Integer\", \"\",1");
    replace_fbx_text(source, "\"FrontAxisSign\", \"int\", \"Integer\", \"\",1",
                     "\"FrontAxisSign\", \"int\", \"Integer\", \"\",-1");
    prepared =
        convert_fbx_interchange(std::as_bytes(std::span(source)), {}, nlohmann::json::object());
    NativeGltfDocument z_up(std::move(prepared.generated));
    const auto& hierarchy = z_up.hierarchy();
    std::vector<AffineTransform> matrices(hierarchy.nodes.size());
    bool converted = false;
    for (const auto i : hierarchy.parent_first) {
        const auto& node = hierarchy.nodes[i];
        AffineTransform local;
        for (unsigned row = 0; row < 3; ++row)
            for (unsigned column = 0; column < 4; ++column)
                local.m[row * 4 + column] = node.matrix[column * 4 + row];
        matrices[i] = node.parent == gltf_no_index ? local : matrices[node.parent] * local;
        if (node.mesh != gltf_no_index) {
            const auto origin = matrices[i].point({0, 0, 0});
            const auto up = matrices[i].vector({0, 0, 1});
            check(std::abs(origin[0] - 2) < 1e-8 && std::abs(origin[1] - 4) < 1e-8 &&
                      std::abs(origin[2] + 3) < 1e-8 && std::abs(up[1] - 1) < 1e-8,
                  "FBX Z-up transforms did not reach canonical Y-up world space");
            converted = true;
        }
    }
    check(converted, "FBX Z-up fixture lost its mesh node");
    source = test::fbx_triangle();
    source.insert(source.find(" Layer: 0 {"), R"fbx( LayerElementTangent: 0 {
  Version: 102
  Name: "UVMap"
  MappingInformationType: "ByPolygonVertex"
  ReferenceInformationType: "Direct"
  Tangents: *9 { a:1,0,0,1,0,0,1,0,0 }
 }
 LayerElementBinormal: 0 {
  Version: 102
  Name: "UVMap"
  MappingInformationType: "ByPolygonVertex"
  ReferenceInformationType: "Direct"
  Binormals: *9 { a:0,1,0,0,1,0,0,1,0 }
 }
)fbx");
    replace_fbx_text(source, " Layer: 0 {\n  Version: 100\n", R"fbx( Layer: 0 {
  Version: 100
  LayerElement: { Type: "LayerElementTangent"
   TypedIndex: 0
  }
  LayerElement: { Type: "LayerElementBinormal"
   TypedIndex: 0
  }
)fbx");
    prepared =
        convert_fbx_interchange(std::as_bytes(std::span(source)), {}, nlohmann::json::object());
    NativeGltfDocument tangent_source(std::move(prepared.generated));
    check(tangent_source.primitive(0, 0).attributes.at("TANGENT").values ==
              std::vector<float>{1, 0, 0, -1, 1, 0, 0, -1, 1, 0, 0, -1},
          "FBX tangent handedness did not follow the converted image V coordinate");
    source = test::fbx_animated_triangle(true);
    replace_fbx_text(source, "Weights: *3 { a:1,1,1 }", "Weights: *3 { a:0.2,0.2,0.2 }");
    for (unsigned i = 1; i <= 4; ++i) {
        const auto joint = std::to_string(3100 + i), cluster = std::to_string(3200 + i);
        source.insert(source.find("}\nConnections:"),
                      "Model: " + joint +
                          ", \"Model::Extra\", \"LimbNode\" { Version: 232\n}\n"
                          "Deformer: " +
                          cluster +
                          ", \"SubDeformer::Extra\", \"Cluster\" { Version: 100\n"
                          " Indexes: *3 { a:0,1,2 }\n Weights: *3 { a:0.2,0.2,0.2 }\n"
                          " Transform: *16 { a:1,0,0,0,0,1,0,0,0,0,1,0,2,3,4,1 }\n"
                          " TransformLink: *16 { a:1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1 }\n}\n");
        source.insert(source.rfind('}'), " C: \"OO\"," + joint + ",0\n C: \"OO\"," + cluster +
                                             ",3001\n C: \"OO\"," + joint + "," + cluster + "\n");
    }
    prepared =
        convert_fbx_interchange(std::as_bytes(std::span(source)), {}, nlohmann::json::object());
    NativeGltfDocument weighted(std::move(prepared.generated));
    rejects([&] { (void)cook_gltf_geometry_bundle(weighted); },
            "FBX extra weights bypassed the shared reject policy");
    GltfModelCookOptions reduced;
    reduced.skin_influences = ExcessSkinInfluences::ReduceToFour;
    const auto reduced_files = cook_gltf_geometry_bundle(weighted, reduced);
    const auto reduced_index = validate_model_bundle(reduced_files, ModelValidation::GeometryStage);
    check(!reduced_index.diagnostics.empty(),
          "FBX influence reduction did not retain its diagnostics");
    for (bool cropped : {false, true}) {
        source = test::fbx_animated_triangle();
        if (cropped) {
            replace_fbx_text(source, "\"LocalStart\", \"KTime\", \"Time\", \"\",0",
                             "\"LocalStart\", \"KTime\", \"Time\", \"\",11546539500");
            replace_fbx_text(source, "\"LocalStop\", \"KTime\", \"Time\", \"\",46186158000",
                             "\"LocalStop\", \"KTime\", \"Time\", \"\",34639618500");
        } else
            replace_fbx_text(source, "\"LocalStop\", \"KTime\", \"Time\", \"\",46186158000",
                             "\"LocalStop\", \"KTime\", \"Time\", \"\",92372316000");
        prepared =
            convert_fbx_interchange(std::as_bytes(std::span(source)), {}, nlohmann::json::object());
        NativeGltfDocument native(std::move(prepared.generated));
        const auto clip = native.animation(0);
        check(std::abs(clip.duration - (cropped ? .5 : 2.)) < 1e-6,
              "FBX clip lost its declared cropped/held duration");
        bool found = false;
        for (const auto& track : clip.tracks)
            if (track.path == NativeAnimationPath::Translation) {
                check(track.times->front() == 0 && track.times->back() == (cropped ? .5f : 2.f) &&
                          std::abs(track.values->at(1) - (cropped ? 3.5f : 3.f)) < 1e-5 &&
                          std::abs(track.values->at(track.values->size() - 2) -
                                   (cropped ? 4.5f : 5.f)) < 1e-5,
                      "FBX clip endpoint interpolation/hold is incorrect");
                found = true;
            }
        check(found, "FBX cropped clip lost its translation channel");
    }
    source = test::fbx_animated_triangle();
    replace_fbx_text(
        source, "  P: \"Lcl Scaling\"",
        "  P: \"UnimplementedValue\", \"Number\", \"\", \"A\",0\n  P: \"Lcl Scaling\"");
    source.insert(source.rfind('}'), " C: \"OP\",2002,1002,\"UnimplementedValue\"\n");
    prepared =
        convert_fbx_interchange(std::as_bytes(std::span(source)), {}, nlohmann::json::object());
    check(std::any_of(prepared.generated.diagnostics.begin(), prepared.generated.diagnostics.end(),
                      [](const auto& message) {
                          return message.find("UnimplementedValue") != std::string::npos;
                      }),
          "Unsupported FBX animated property was silently discarded");
}
} // namespace
