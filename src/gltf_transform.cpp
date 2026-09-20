#include "gltf_transform.hpp"
#include <cmath>
namespace forge::asset_detail {
namespace {
using Json = nlohmann::json;
double number(const Json& value) {
    if (!value.is_number() || !std::isfinite(value.get<double>()))
        throw std::runtime_error("glTF transform value must be finite");
    return value.get<double>();
}
template <std::size_t N>
std::array<double, N> numbers(const Json& object, const char* key, std::array<double, N> defaults) {
    if (!object.contains(key))
        return defaults;
    const auto& values = object.at(key);
    if (!values.is_array() || values.size() != N)
        throw std::runtime_error(std::string("glTF invalid vector/matrix shape: ") + key);
    for (std::size_t i = 0; i < N; ++i)
        defaults[i] = number(values[i]);
    return defaults;
}
} // namespace
std::array<double, 16> gltf_node_matrix(const Json& node) {
    std::array<double, 16> result{1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
    if (node.contains("matrix")) {
        if (node.contains("translation") || node.contains("rotation") || node.contains("scale"))
            throw std::runtime_error("glTF node cannot define both matrix and TRS");
        result = numbers(node, "matrix", result);
        if (result[3] != 0 || result[7] != 0 || result[11] != 0 || result[15] != 1)
            throw std::runtime_error("glTF node matrix must be affine");
        std::array<std::array<double, 3>, 3> columns;
        for (unsigned c = 0; c < 3; ++c) {
            const double length = std::hypot(result[c * 4], result[c * 4 + 1], result[c * 4 + 2]);
            if (!std::isfinite(length) || length == 0)
                throw std::runtime_error("glTF node matrix has a singular column");
            for (unsigned r = 0; r < 3; ++r)
                columns[c][r] = result[c * 4 + r] / length;
        }
        for (unsigned a = 0; a < 3; ++a)
            for (unsigned b = a + 1; b < 3; ++b) {
                double dot = 0;
                for (unsigned r = 0; r < 3; ++r)
                    dot += columns[a][r] * columns[b][r];
                if (std::abs(dot) > 1e-6)
                    throw std::runtime_error("glTF node matrix contains non-TRS shear");
            }
        return result;
    }
    const auto translation = numbers<3>(node, "translation", {0, 0, 0});
    const auto scale = numbers<3>(node, "scale", {1, 1, 1});
    auto q = numbers<4>(node, "rotation", {0, 0, 0, 1});
    const double norm = std::hypot(std::hypot(q[0], q[1]), std::hypot(q[2], q[3]));
    if (!std::isfinite(norm) || std::abs(norm - 1) > 0.001)
        throw std::runtime_error("glTF node rotation must be a unit quaternion");
    for (auto& value : q)
        value /= norm;
    const auto x = q[0], y = q[1], z = q[2], w = q[3];
    result = {1 - 2 * (y * y + z * z), 2 * (x * y + z * w),     2 * (x * z - y * w),     0,
              2 * (x * y - z * w),     1 - 2 * (x * x + z * z), 2 * (y * z + x * w),     0,
              2 * (x * z + y * w),     2 * (y * z - x * w),     1 - 2 * (x * x + y * y), 0,
              translation[0],          translation[1],          translation[2],          1};
    for (unsigned c = 0; c < 3; ++c)
        for (unsigned r = 0; r < 3; ++r) {
            result[c * 4 + r] *= scale[c];
            if (!std::isfinite(result[c * 4 + r]))
                throw std::runtime_error("glTF TRS matrix exceeds finite numeric range");
        }
    return result;
}
} // namespace forge::asset_detail
