#pragma once
#include <array>
#include <stdexcept>
namespace forge {
// Shared value representation for material assets and object-local values.
// This header owns no source document, resource lease or native GPU object.
enum class MaterialParameterType { Scalar, Vector2, Vector3, Vector4, LinearColor3, LinearColor4 };
struct MaterialParameter {
    MaterialParameterType type = MaterialParameterType::Scalar;
    // Unused lanes must be zero. Linear colors never pass through sRGB decoding.
    std::array<float, 4> value{};
    bool operator==(const MaterialParameter&) const = default;
};
inline unsigned material_parameter_width(MaterialParameterType type) {
    switch (type) {
    case MaterialParameterType::Scalar:
        return 1;
    case MaterialParameterType::Vector2:
        return 2;
    case MaterialParameterType::Vector3:
    case MaterialParameterType::LinearColor3:
        return 3;
    case MaterialParameterType::Vector4:
    case MaterialParameterType::LinearColor4:
        return 4;
    }
    throw std::runtime_error("Unknown material parameter type");
}
} // namespace forge
