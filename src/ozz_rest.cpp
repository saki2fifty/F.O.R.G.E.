#include "gltf_transform.hpp"
#include <cmath>
#include <forge/transform.hpp>
namespace forge::asset_detail {
namespace {
using Json = nlohmann::json;
void require(bool ok, const char* why) {
    if (!ok)
        throw std::runtime_error(why);
}
float scalar(double x) {
    require(std::isfinite(x) && std::abs(x) <= 65504 && (x == 0 || static_cast<float>(x) != 0),
            "Animation rest value cannot be represented by the admitted Ozz profile");
    return static_cast<float>(x);
}
} // namespace
Json canonical_ozz_rest(const Json& source) {
    const auto admitted = gltf_node_matrix(source);
    std::array<float, 3> t{}, s{1, 1, 1};
    LocalRotation rotation;
    if (source.contains("matrix")) {
        AffineTransform matrix;
        for (unsigned r = 0; r < 3; ++r)
            for (unsigned c = 0; c < 4; ++c)
                matrix.m[r * 4 + c] = admitted[c * 4 + r];
        // Normalize columns before using the authored-TRS decomposition helper.
        // Ozz rest data has its own numeric profile; it is not an ECS LocalScale
        // write and must not accidentally inherit the authored 10000 limit.
        std::array<double, 3> lengths{};
        for (unsigned c = 0; c < 3; ++c) {
            lengths[c] = std::hypot(matrix.m[c], matrix.m[4 + c], matrix.m[8 + c]);
            require(lengths[c] > 0, "Matrix-authored rest transform is singular");
            scalar(lengths[c]);
            for (unsigned r = 0; r < 3; ++r)
                matrix.m[r * 4 + c] /= lengths[c];
        }
        const auto local = decompose(matrix);
        t = {scalar(local.translation.x), scalar(local.translation.y), scalar(local.translation.z)};
        s = {scalar(local.scale.x * lengths[0]), scalar(local.scale.y * lengths[1]),
             scalar(local.scale.z * lengths[2])};
        rotation = local.rotation;
    } else {
        if (source.contains("translation"))
            for (unsigned i = 0; i < 3; ++i)
                t[i] = scalar(source.at("translation").at(i).get<double>());
        if (source.contains("scale"))
            for (unsigned i = 0; i < 3; ++i)
                s[i] = scalar(source.at("scale").at(i).get<double>());
        if (source.contains("rotation")) {
            const auto& q = source.at("rotation");
            rotation = normalized({scalar(q.at(0).get<double>()), scalar(q.at(1).get<double>()),
                                   scalar(q.at(2).get<double>()), scalar(q.at(3).get<double>())});
        }
    }
    return {{"translation", t},
            {"rotation", {rotation.x, rotation.y, rotation.z, rotation.w}},
            {"scale", s}};
}
} // namespace forge::asset_detail
