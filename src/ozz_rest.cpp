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
    const auto trs = canonical_gltf_trs(source);
    std::array<float, 3> t{}, s{};
    for (unsigned i = 0; i < 3; ++i) {
        t[i] = scalar(trs.at("translation").at(i).get<double>());
        s[i] = scalar(trs.at("scale").at(i).get<double>());
    }
    const auto& q = trs.at("rotation");
    const auto rotation =
        normalized({scalar(q.at(0).get<double>()), scalar(q.at(1).get<double>()),
                    scalar(q.at(2).get<double>()), scalar(q.at(3).get<double>())});
    return {{"translation", t},
            {"rotation", {rotation.x, rotation.y, rotation.z, rotation.w}},
            {"scale", s}};
}
} // namespace forge::asset_detail
