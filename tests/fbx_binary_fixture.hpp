#pragma once
#include <bit>
#include <cstdint>
#include <string>
#include <vector>
namespace forge::test {
// Original test-only binary FBX construction. This is not a general exporter.
// Absolute record endpoints and property scalar widths follow the file format;
// no parser implementation or third-party model bytes are embedded here.
struct FbxBinaryNode {
    std::string name, properties;
    std::uint64_t count = 0;
    std::vector<FbxBinaryNode> children;
};
inline void fbx_binary_integer(std::string& out, std::uint64_t value, unsigned width) {
    for (unsigned i = 0; i < width; ++i)
        out.push_back(char((value >> (i * 8)) & 255));
}
inline std::string fbx_binary_string(const std::string& text) {
    std::string out = "S";
    fbx_binary_integer(out, text.size(), 4);
    return out + text;
}
inline std::string fbx_binary_i64(std::int64_t value) {
    std::string out = "L";
    fbx_binary_integer(out, static_cast<std::uint64_t>(value), 8);
    return out;
}
inline std::string fbx_binary_double(double value) {
    std::string out = "D";
    fbx_binary_integer(out, std::bit_cast<std::uint64_t>(value), 8);
    return out;
}
inline FbxBinaryNode fbx_binary_property(const std::string& name,
                                         const std::vector<double>& values) {
    FbxBinaryNode result{"P",
                         fbx_binary_string(name) + fbx_binary_string("Number") +
                             fbx_binary_string("") + fbx_binary_string("A"),
                         4,
                         {}};
    for (auto value : values) {
        result.properties += fbx_binary_double(value);
        ++result.count;
    }
    return result;
}
inline void fbx_binary_record(std::string& out, const FbxBinaryNode& node, unsigned width) {
    const auto begin = out.size();
    out.append(width * 3, '\0');
    out.push_back(char(node.name.size()));
    out += node.name;
    out += node.properties;
    for (const auto& child : node.children)
        fbx_binary_record(out, child, width);
    if (!node.children.empty())
        out.append(width * 3 + 1, '\0');
    std::string header;
    fbx_binary_integer(header, out.size(), width);
    fbx_binary_integer(header, node.count, width);
    fbx_binary_integer(header, node.properties.size(), width);
    out.replace(begin, header.size(), header);
}
inline std::string fbx_binary_triangle(unsigned version = 7400, bool reversed = false) {
    const auto width = version >= 7500 ? 8u : 4u;
    std::string out("Kaydara FBX Binary  \0\x1a\0", 23);
    fbx_binary_integer(out, version, 4);
    FbxBinaryNode global{
        "GlobalSettings",
        "",
        0,
        {{"Properties70",
          "",
          0,
          {fbx_binary_property("UpAxis", {1}), fbx_binary_property("UpAxisSign", {1}),
           fbx_binary_property("FrontAxis", {2}), fbx_binary_property("FrontAxisSign", {1}),
           fbx_binary_property("CoordAxis", {0}), fbx_binary_property("CoordAxisSign", {1}),
           fbx_binary_property("UnitScaleFactor", {100})}}}};
    std::string vertices = "d";
    fbx_binary_integer(vertices, 9, 4);
    fbx_binary_integer(vertices, 0, 4);
    fbx_binary_integer(vertices, 72, 4);
    for (double value : {0., 0., 0., 1., 0., 0., 0., 1., 0.})
        fbx_binary_integer(vertices, std::bit_cast<std::uint64_t>(value), 8);
    std::string polygon = "i";
    fbx_binary_integer(polygon, 3, 4);
    fbx_binary_integer(polygon, 0, 4);
    fbx_binary_integer(polygon, 12, 4);
    for (std::int32_t value : {0, 1, -3})
        fbx_binary_integer(polygon, static_cast<std::uint32_t>(value), 4);
    FbxBinaryNode mesh{"Geometry",
                       fbx_binary_i64(1001) +
                           fbx_binary_string(std::string("Triangle\0\1Geometry", 18)) +
                           fbx_binary_string("Mesh"),
                       3,
                       {{"Vertices", vertices, 1, {}}, {"PolygonVertexIndex", polygon, 1, {}}}};
    FbxBinaryNode model{"Model",
                        fbx_binary_i64(1002) +
                            fbx_binary_string(std::string("Triangle\0\1Model", 15)) +
                            fbx_binary_string("Mesh"),
                        3,
                        {{"Properties70",
                          "",
                          0,
                          {fbx_binary_property("Lcl Translation", {2, 3, 4}),
                           fbx_binary_property("Lcl Rotation", {0, 0, 0}),
                           fbx_binary_property("Lcl Scaling", {1, 1, 1})}}}};
    FbxBinaryNode objects{"Objects", "", 0,
                          reversed ? std::vector{model, mesh} : std::vector{mesh, model}};
    FbxBinaryNode connections{
        "Connections",
        "",
        0,
        {{"C", fbx_binary_string("OO") + fbx_binary_i64(1001) + fbx_binary_i64(1002), 3, {}},
         {"C", fbx_binary_string("OO") + fbx_binary_i64(1002) + fbx_binary_i64(0), 3, {}}}};
    for (const auto& node : {global, objects, connections})
        fbx_binary_record(out, node, width);
    out.append(width * 3 + 1, '\0');
    return out;
}
} // namespace forge::test
