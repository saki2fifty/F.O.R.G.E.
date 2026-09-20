#include "gltf_validation.hpp"
#include <bit>
#include <cmath>
#include <forge/gltf_accessors.hpp>

namespace forge {
namespace {
using namespace gltf_detail;
struct Layout {
    unsigned bytes, columns, rows, column_bytes, element_bytes;
};
Layout layout(std::size_t type, const std::string& shape) {
    Layout value{};
    switch (type) {
    case 5120:
    case 5121:
        value.bytes = 1;
        break;
    case 5122:
    case 5123:
        value.bytes = 2;
        break;
    case 5125:
    case 5126:
        value.bytes = 4;
        break;
    default:
        throw std::runtime_error("Unsupported glTF accessor componentType");
    }
    value.columns = 1;
    if (shape == "SCALAR")
        value.rows = 1;
    else if (shape == "VEC2")
        value.rows = 2;
    else if (shape == "VEC3")
        value.rows = 3;
    else if (shape == "VEC4")
        value.rows = 4;
    else if (shape == "MAT2")
        value.columns = value.rows = 2;
    else if (shape == "MAT3")
        value.columns = value.rows = 3;
    else if (shape == "MAT4")
        value.columns = value.rows = 4;
    else
        throw std::runtime_error("Unsupported glTF accessor type");
    value.column_bytes = value.bytes * value.rows;
    if (value.columns != 1)
        value.column_bytes = (value.column_bytes + 3) & ~3u;
    value.element_bytes = value.columns * value.column_bytes;
    return value;
}
std::uint32_t unsigned_value(std::span<const std::byte> data) {
    std::uint32_t result = 0;
    for (std::size_t i = 0; i < data.size(); ++i)
        result |= std::uint32_t(std::to_integer<unsigned char>(data[i])) << (8 * i);
    return result;
}
void numeric_bound(const Json& value, std::size_t type) {
    if (!value.is_number())
        throw std::runtime_error("glTF accessor bound must be numeric");
    const auto number = value.get<double>();
    const auto [minimum, maximum] = [&]() -> std::pair<double, double> {
        switch (type) {
        case 5120:
            return {-128, 127};
        case 5121:
            return {0, 255};
        case 5122:
            return {-32768, 32767};
        case 5123:
            return {0, 65535};
        case 5125:
            return {0, 4294967295.0};
        default:
            return {-std::numeric_limits<float>::max(), std::numeric_limits<float>::max()};
        }
    }();
    if (!std::isfinite(number) || number < minimum || number > maximum ||
        (type != 5126 && std::trunc(number) != number))
        throw std::runtime_error("glTF accessor bound is outside component numeric range");
}
} // namespace
GltfAccessorAdmission validate_gltf_accessors(const GltfSourceBundle& source,
                                              GltfAccessorLimits limits, std::stop_token cancel) {
    if (!limits.elements_per_accessor || limits.elements_per_accessor > 64 * 1024 * 1024 ||
        !limits.total_components || limits.total_components > 512 * 1024 * 1024)
        throw std::runtime_error("Invalid glTF accessor admission limits");
    const auto& accessors = array(source.document, "accessors", 1000000);
    const auto& views = array(source.document, "bufferViews", 1000000);
    GltfAccessorAdmission result;
    auto cancelled = [&] {
        if (cancel.stop_requested())
            throw std::runtime_error("glTF accessor validation cancelled");
    };
    // Revalidate ranges rather than trusting a mutable caller-created bundle.
    auto range = [&](std::size_t view_index, std::size_t offset, std::size_t count,
                     const Layout& format, bool sparse) {
        if (view_index >= views.size())
            throw std::runtime_error("glTF accessor bufferView index is invalid");
        const auto& view = views[view_index];
        const auto buffer = size_value(view.at("buffer"));
        const auto begin = size_or(view, "byteOffset", 0),
                   length = size_value(view.at("byteLength"));
        if (buffer >= source.buffers.size() || !length || begin > source.buffers[buffer].length ||
            length > source.buffers[buffer].length - begin)
            throw std::runtime_error("glTF accessor bufferView exceeds captured buffer");
        if (offset % format.bytes || begin % format.bytes)
            throw std::runtime_error("glTF accessor component alignment is invalid");
        if (sparse && (view.contains("byteStride") || view.contains("target")))
            throw std::runtime_error("glTF sparse view cannot define byteStride or target");
        const auto stride = size_or(view, "byteStride", format.element_bytes);
        if (stride < format.element_bytes || stride % format.bytes ||
            (view.contains("byteStride") && (stride < 4 || stride > 252 || stride % 4)))
            throw std::runtime_error("glTF accessor stride is invalid");
        if (offset > length || format.element_bytes > length - offset ||
            (count - 1) > (length - offset - format.element_bytes) / stride)
            throw std::runtime_error("glTF accessor elements exceed bufferView");
        return std::pair{source.buffers[buffer].bytes().subspan(begin + offset, length - offset),
                         stride};
    };
    auto finite = [&](std::span<const std::byte> data, std::size_t stride, std::size_t count,
                      const Layout& format) {
        for (std::size_t i = 0; i < count; ++i) {
            if (i % 4096 == 0)
                cancelled();
            for (unsigned column = 0; column < format.columns; ++column)
                for (unsigned row = 0; row < format.rows; ++row) {
                    const auto offset = i * stride + column * format.column_bytes + row * 4;
                    if (!std::isfinite(
                            std::bit_cast<float>(unsigned_value(data.subspan(offset, 4)))))
                        throw std::runtime_error("glTF accessor contains non-finite float data");
                }
        }
    };
    for (std::size_t index = 0; index < accessors.size(); ++index) {
        cancelled();
        try {
            const auto& accessor = accessors[index];
            const auto type = size_value(accessor.at("componentType"));
            const auto format = layout(type, accessor.at("type").get<std::string>());
            const auto count = size_value(accessor.at("count"));
            if (!count || count > limits.elements_per_accessor ||
                count >
                    (limits.total_components - result.components) / (format.columns * format.rows))
                throw std::runtime_error("glTF accessor count/aggregate component limit exceeded");
            result.components += count * format.columns * format.rows;
            result.elements += count;
            ++result.accessors;
            if (accessor.contains("normalized")) {
                if (!accessor.at("normalized").is_boolean())
                    throw std::runtime_error("glTF accessor normalized must be boolean");
                if (accessor.at("normalized").get<bool>() && (type == 5125 || type == 5126))
                    throw std::runtime_error(
                        "glTF normalized only applies to byte/short component types");
            }
            for (const auto* field : {"min", "max"}) {
                if (!accessor.contains(field))
                    continue;
                const auto& bounds = accessor.at(field);
                if (!bounds.is_array() || bounds.size() != format.columns * format.rows)
                    throw std::runtime_error("glTF accessor bound dimension differs from type");
                for (const auto& value : bounds)
                    numeric_bound(value, type);
            }
            if (accessor.contains("min") && accessor.contains("max"))
                for (std::size_t i = 0; i < accessor.at("min").size(); ++i)
                    if (accessor.at("min")[i].get<double>() > accessor.at("max")[i].get<double>())
                        throw std::runtime_error("glTF accessor minimum exceeds maximum");
            const auto offset = size_or(accessor, "byteOffset", 0);
            if (accessor.contains("bufferView")) {
                const auto data =
                    range(size_value(accessor.at("bufferView")), offset, count, format, false);
                if (type == 5126)
                    finite(data.first, data.second, count, format);
            } else if (accessor.contains("byteOffset")) {
                throw std::runtime_error("glTF accessor without bufferView cannot have byteOffset");
            }
            if (accessor.contains("sparse")) {
                const auto& sparse = accessor.at("sparse");
                const auto sparse_count = size_value(sparse.at("count"));
                if (!sparse_count || sparse_count > count)
                    throw std::runtime_error("glTF sparse count exceeds accessor count or is zero");
                const auto& indices = sparse.at("indices");
                const auto index_type = size_value(indices.at("componentType"));
                if (index_type != 5121 && index_type != 5123 && index_type != 5125)
                    throw std::runtime_error("glTF sparse indices require unsigned byte/short/int");
                const auto index_format = layout(index_type, "SCALAR");
                const auto index_data =
                    range(size_value(indices.at("bufferView")), size_or(indices, "byteOffset", 0),
                          sparse_count, index_format, true);
                std::uint32_t previous = 0;
                for (std::size_t i = 0; i < sparse_count; ++i) {
                    if (i % 4096 == 0)
                        cancelled();
                    const auto item = unsigned_value(
                        index_data.first.subspan(i * index_format.bytes, index_format.bytes));
                    if (item >= count || (i && item <= previous))
                        throw std::runtime_error(
                            "glTF sparse indices must increase strictly and remain in range");
                    previous = item;
                }
                const auto& values = sparse.at("values");
                const auto value_data =
                    range(size_value(values.at("bufferView")), size_or(values, "byteOffset", 0),
                          sparse_count, format, true);
                if (type == 5126)
                    finite(value_data.first, value_data.second, sparse_count, format);
                result.sparse_replacements += sparse_count;
            }
        } catch (const std::exception& error) {
            throw std::runtime_error("glTF accessor " + std::to_string(index) + ": " +
                                     error.what());
        }
    }
    return result;
}
} // namespace forge
