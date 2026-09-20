#include <bit>
#include <forge/gltf_accessors.hpp>
#include <iostream>

using namespace forge;
namespace {
using Json = nlohmann::json;
using Bytes = std::vector<std::byte>;
void require(bool value, const char* message) {
    if (!value)
        throw std::runtime_error(message);
}
template <class F> void rejects(F fn, std::string_view expected = {}) {
    try {
        fn();
    } catch (const std::exception& error) {
        if (std::string_view(error.what()).find(expected) != std::string_view::npos)
            return;
        throw std::runtime_error("Unexpected accessor rejection: " + std::string(error.what()));
    }
    throw std::runtime_error("Invalid glTF accessor accepted");
}
GltfSourceBundle bundle(Json accessors, Json views, Bytes bytes) {
    GltfSourceBundle result;
    result.document = {{"accessors", std::move(accessors)}, {"bufferViews", std::move(views)}};
    auto data = std::make_shared<Bytes>(std::move(bytes));
    result.buffers.push_back({data, 0, data->size()});
    return result;
}
Json accessor(unsigned type, const char* shape, std::size_t count) {
    return {{"bufferView", 0}, {"componentType", type}, {"type", shape}, {"count", count}};
}
Json view(std::size_t length, std::size_t offset = 0) {
    return {{"buffer", 0}, {"byteOffset", offset}, {"byteLength", length}};
}
void put_float(Bytes& bytes, std::size_t offset, float value) {
    const auto bits = std::bit_cast<std::uint32_t>(value);
    for (unsigned i = 0; i < 4; ++i)
        bytes.at(offset + i) = std::byte((bits >> (i * 8)) & 255);
}
} // namespace
int main() {
    try {
        for (unsigned type : {5120u, 5121u, 5122u, 5123u, 5125u, 5126u}) {
            const unsigned width = type < 5122 ? 1 : type < 5125 ? 2 : 4;
            for (const auto* shape : {"SCALAR", "VEC2", "VEC3", "VEC4", "MAT2", "MAT3", "MAT4"}) {
                const unsigned rows =
                    std::string_view(shape) == "SCALAR" ? 1 : unsigned(shape[3] - '0');
                const bool matrix = shape[0] == 'M';
                const auto element = matrix ? rows * ((rows * width + 3) & ~3u) : rows * width;
                auto data = bundle(Json::array({accessor(type, shape, 2)}),
                                   Json::array({view(element * 2)}), Bytes(element * 2));
                require(validate_gltf_accessors(data).elements == 2,
                        "Core accessor layout rejected");
                data.document["bufferViews"][0]["byteLength"] = element * 2 - 1;
                rejects([&] { (void)validate_gltf_accessors(data); }, "exceed bufferView");
            }
        }
        // Official specification's 23-byte interleaved layout: no unused trailing
        // stride padding is required after the last actual accessor element.
        auto uv = accessor(5123, "VEC2", 3), color = accessor(5121, "VEC3", 3);
        uv["normalized"] = true;
        color["normalized"] = true;
        color["byteOffset"] = 4;
        auto interleaved_view = view(23);
        interleaved_view["byteStride"] = 8;
        auto interleaved =
            bundle(Json::array({uv, color}), Json::array({interleaved_view}), Bytes(23));
        require(validate_gltf_accessors(interleaved).components == 15,
                "Interleaved normalized attributes rejected");
        auto invalid = interleaved;
        invalid.document["bufferViews"][0]["byteStride"] = 5;
        rejects([&] { (void)validate_gltf_accessors(invalid); }, "stride");
        invalid = interleaved;
        invalid.document["accessors"][0]["byteOffset"] = 1;
        rejects([&] { (void)validate_gltf_accessors(invalid); }, "alignment");

        auto sparse_accessor = accessor(5126, "VEC3", 5);
        sparse_accessor.erase("bufferView");
        sparse_accessor["sparse"] = {{"count", 2},
                                     {"indices", {{"bufferView", 0}, {"componentType", 5121}}},
                                     {"values", {{"bufferView", 1}}}};
        Bytes sparse_bytes(28);
        sparse_bytes[0] = std::byte{1};
        sparse_bytes[1] = std::byte{4};
        put_float(sparse_bytes, 4, 1.25f);
        put_float(sparse_bytes, 16, -3.0f);
        auto sparse = bundle(Json::array({sparse_accessor}), Json::array({view(2), view(24, 4)}),
                             sparse_bytes);
        require(validate_gltf_accessors(sparse).sparse_replacements == 2,
                "Zero-backed sparse accessor rejected");
        for (auto indices : {std::pair{1, 1}, std::pair{4, 1}, std::pair{1, 5}}) {
            auto bad_bytes = sparse_bytes;
            bad_bytes[0] = std::byte(indices.first);
            bad_bytes[1] = std::byte(indices.second);
            auto bad =
                bundle(sparse.document["accessors"], sparse.document["bufferViews"], bad_bytes);
            rejects([&] { (void)validate_gltf_accessors(bad); }, "increase strictly");
        }
        invalid = sparse;
        invalid.document["bufferViews"][1]["target"] = 34962;
        rejects([&] { (void)validate_gltf_accessors(invalid); }, "cannot define");
        invalid = sparse;
        invalid.document["accessors"][0]["sparse"]["count"] = 6;
        rejects([&] { (void)validate_gltf_accessors(invalid); }, "sparse count");
        invalid = sparse;
        invalid.document["accessors"][0]["byteOffset"] = 0;
        rejects([&] { (void)validate_gltf_accessors(invalid); }, "without bufferView");
        invalid = sparse;
        invalid.document["accessors"][0].erase("sparse");
        require(validate_gltf_accessors(invalid).elements == 5, "Implicit zero accessor rejected");

        for (float number :
             {std::numeric_limits<float>::infinity(), -std::numeric_limits<float>::infinity(),
              std::numeric_limits<float>::quiet_NaN()}) {
            auto bad_bytes = sparse_bytes;
            put_float(bad_bytes, 4, number);
            auto bad =
                bundle(sparse.document["accessors"], sparse.document["bufferViews"], bad_bytes);
            rejects([&] { (void)validate_gltf_accessors(bad); }, "non-finite");
            bad.document["accessors"] = Json::array({accessor(5126, "SCALAR", 7)});
            bad.document["bufferViews"] = Json::array({view(28)});
            rejects([&] { (void)validate_gltf_accessors(bad); }, "non-finite");
        }
        auto bounded =
            bundle(Json::array({accessor(5121, "SCALAR", 1)}), Json::array({view(1)}), Bytes(1));
        for (const auto& count : {Json(0), Json(-1), Json(true), Json(UINT64_MAX), Json(1.5)}) {
            invalid = bounded;
            invalid.document["accessors"][0]["count"] = count;
            rejects([&] { (void)validate_gltf_accessors(invalid); });
        }
        invalid = bounded;
        invalid.document["accessors"][0]["byteOffset"] = UINT64_MAX;
        rejects([&] { (void)validate_gltf_accessors(invalid); }, "exceed bufferView");
        invalid = bounded;
        invalid.document["accessors"][0]["min"] = Json::array({-1});
        rejects([&] { (void)validate_gltf_accessors(invalid); }, "numeric range");
        invalid = bounded;
        invalid.document["accessors"][0]["min"] = Json::array({3});
        invalid.document["accessors"][0]["max"] = Json::array({2});
        rejects([&] { (void)validate_gltf_accessors(invalid); }, "minimum exceeds");
        invalid = bounded;
        invalid.document["accessors"][0]["min"] = Json::array({0, 0});
        rejects([&] { (void)validate_gltf_accessors(invalid); }, "dimension");
        invalid = bounded;
        invalid.document["accessors"][0]["componentType"] = 5126;
        invalid.document["accessors"][0]["normalized"] = true;
        rejects([&] { (void)validate_gltf_accessors(invalid); }, "normalized");
        GltfAccessorLimits budget;
        budget.total_components = 1;
        rejects([&] { (void)validate_gltf_accessors(interleaved, budget); }, "limit");
        std::stop_source stop;
        stop.request_stop();
        rejects([&] { (void)validate_gltf_accessors(sparse, {}, stop.get_token()); }, "cancelled");
        std::cout << "glTF accessor ranges, matrix padding, sparse order, normalized types and "
                     "numeric bounds passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
