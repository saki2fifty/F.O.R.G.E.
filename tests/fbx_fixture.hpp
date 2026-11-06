#pragma once
#include <string>
namespace forge::test {
// Original ASCII FBX, authored for FORGE; no external model fixture or exporter.
inline std::string fbx_triangle(bool reversed = false, double centimetres = 100) {
    const auto header = std::string(R"fbx(; FORGE original triangle
FBXHeaderExtension: { FBXHeaderVersion: 1003
 FBXVersion: 7400
 Creator: "FORGE test fixture"
}
GlobalSettings: { Version: 1000
 Properties70: {
  P: "UpAxis", "int", "Integer", "",1
  P: "UpAxisSign", "int", "Integer", "",1
  P: "FrontAxis", "int", "Integer", "",2
  P: "FrontAxisSign", "int", "Integer", "",1
  P: "CoordAxis", "int", "Integer", "",0
  P: "CoordAxisSign", "int", "Integer", "",1
  P: "UnitScaleFactor", "double", "Number", "",
)fbx") + std::to_string(centimetres) +
                        "\n }\n}\nObjects: {\n";
    const std::string geometry = R"fbx(Geometry: 1001, "Geometry::Triangle", "Mesh" {
 GeometryVersion: 124
 Vertices: *9 { a:0,0,0,1,0,0,0,1,0 }
 PolygonVertexIndex: *3 { a:0,1,-3 }
 LayerElementNormal: 0 {
  Version: 101
  Name: "Normals"
  MappingInformationType: "ByPolygonVertex"
  ReferenceInformationType: "Direct"
  Normals: *9 { a:0,0,1,0,0,1,0,0,1 }
 }
 LayerElementUV: 0 {
  Version: 101
  Name: "UVMap"
  MappingInformationType: "ByPolygonVertex"
  ReferenceInformationType: "Direct"
  UV: *6 { a:0,0,1,0,0,1 }
 }
 Layer: 0 {
  Version: 100
  LayerElement: { Type: "LayerElementNormal"
   TypedIndex: 0
  }
  LayerElement: { Type: "LayerElementUV"
   TypedIndex: 0
  }
 }
}
)fbx";
    const std::string model = R"fbx(Model: 1002, "Model::Triangle", "Mesh" {
 Version: 232
 Properties70: {
  P: "Lcl Translation", "Lcl Translation", "", "A",2,3,4
  P: "Lcl Rotation", "Lcl Rotation", "", "A",0,0,0
  P: "Lcl Scaling", "Lcl Scaling", "", "A",1,1,1
 }
}
)fbx";
    return header + (reversed ? model + geometry : geometry + model) + R"fbx(}
Connections: {
 C: "OO",1001,1002
 C: "OO",1002,0
}
)fbx";
}
inline std::string fbx_textured_triangle(const std::string& image = "color.png") {
    auto result = fbx_triangle();
    result.insert(result.find("}\nConnections:"), R"fbx(Material: 4000, "Material::Surface", "" {
 Version: 102
 ShadingModel: "lambert"
 Properties70: {
  P: "DiffuseColor", "Color", "", "A",1,1,1
  P: "DiffuseFactor", "Number", "", "A",1
 }
}
Texture: 4001, "Texture::Color", "" {
 Type: "TextureVideoClip"
 Version: 202
 TextureName: "Texture::Color"
 Media: "Video::Color"
 FileName: ")fbx" + image + R"fbx("
 RelativeFilename: ")fbx" + image + R"fbx("
 Properties70: { P: "UVSet", "KString", "", "", "UVMap" }
}
)fbx");
    result.insert(result.rfind('}'), R"fbx( C: "OO",4000,1002
 C: "OP",4001,4000,"DiffuseColor"
)fbx");
    return result;
}
inline std::string fbx_animated_triangle(bool skinned = false) {
    auto result = fbx_triangle();
    const auto object_end = result.find("}\nConnections:");
    result.insert(object_end, R"fbx(AnimationStack: 2000, "AnimStack::Move", "" {
 Properties70: {
  P: "LocalStart", "KTime", "Time", "",0
  P: "LocalStop", "KTime", "Time", "",46186158000
 }
}
AnimationLayer: 2001, "AnimLayer::Base", "" { }
AnimationCurveNode: 2002, "AnimCurveNode::T", "" {
 Properties70: { P: "d|X", "Number", "", "A",2
  P: "d|Y", "Number", "", "A",3
  P: "d|Z", "Number", "", "A",4
 }
}
AnimationCurve: 2003, "AnimCurve::Y", "" {
 Default: 3
 KeyVer: 4008
 KeyTime: *2 { a:0,46186158000 }
 KeyValueFloat: *2 { a:3,5 }
 KeyAttrFlags: *2 { a:4,4 }
 KeyAttrDataFloat: *8 { a:0,0,0,0,0,0,0,0 }
 KeyAttrRefCount: *2 { a:1,1 }
}
)fbx");
    result.insert(result.rfind('}'), R"fbx( C: "OO",2001,2000
 C: "OO",2002,2001
 C: "OP",2002,1002,"Lcl Translation"
 C: "OP",2003,2002,"d|Y"
)fbx");
    if (skinned) {
        result.insert(result.find("}\nConnections:"),
                      R"fbx(Model: 3000, "Model::Joint", "LimbNode" {
 Version: 232
 Properties70: { P: "Lcl Translation", "Lcl Translation", "", "A",0,0,0
  P: "Lcl Rotation", "Lcl Rotation", "", "A",0,0,0
  P: "Lcl Scaling", "Lcl Scaling", "", "A",1,1,1
 }
}
Deformer: 3001, "Deformer::Skin", "Skin" { Version: 101
 SkinningType: "Linear"
}
Deformer: 3002, "SubDeformer::Joint", "Cluster" { Version: 100
 Indexes: *3 { a:0,1,2 }
 Weights: *3 { a:1,1,1 }
 Transform: *16 { a:1,0,0,0,0,1,0,0,0,0,1,0,2,3,4,1 }
 TransformLink: *16 { a:1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1 }
}
)fbx");
        result.insert(result.find("}\nConnections:"),
                      R"fbx(AnimationCurveNode: 3003, "AnimCurveNode::JointT", "" {
 Properties70: { P: "d|X", "Number", "", "A",0
  P: "d|Y", "Number", "", "A",0
  P: "d|Z", "Number", "", "A",0
 }
}
AnimationCurve: 3004, "AnimCurve::JointY", "" {
 Default: 0
 KeyVer: 4008
 KeyTime: *2 { a:0,46186158000 }
 KeyValueFloat: *2 { a:0,2 }
 KeyAttrFlags: *2 { a:4,4 }
 KeyAttrDataFloat: *8 { a:0,0,0,0,0,0,0,0 }
 KeyAttrRefCount: *2 { a:1,1 }
}
)fbx");
        result.insert(result.rfind('}'), R"fbx( C: "OO",3003,2001
 C: "OP",3003,3000,"Lcl Translation"
 C: "OP",3004,3003,"d|Y"
 C: "OO",3000,0
 C: "OO",3001,1001
 C: "OO",3002,3001
 C: "OO",3000,3002
)fbx");
    }
    return result;
}
} // namespace forge::test
