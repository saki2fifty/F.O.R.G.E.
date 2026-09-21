#include "gltf_native.hpp"
#include "gltf_draco.hpp"
#include "gltf_instances.hpp"
#include "gltf_meshopt.hpp"
#include "gltf_scene.hpp"
#include "gltf_surfaces.hpp"
#include <GLTFDocument.hpp>
#include <GLTFVertexDataConverter.hpp>
#include <algorithm>
#include <tiny_gltf.h>
#include <type_traits>

namespace forge::asset_detail {
namespace {
template <class T> NativeGltfValues<T> convert(const tinygltf::Model& model, std::size_t index) {
    using namespace Diligent;
    if (index >= model.accessors.size())
        throw std::runtime_error("Native glTF accessor index is invalid");
    const auto& accessor = model.accessors[index];
    VALUE_TYPE source_type = VT_UNDEFINED;
    unsigned component_bytes = 0;
    switch (accessor.componentType) {
    case TINYGLTF_COMPONENT_TYPE_BYTE:
        source_type = VT_INT8;
        component_bytes = 1;
        break;
    case TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE:
        source_type = VT_UINT8;
        component_bytes = 1;
        break;
    case TINYGLTF_COMPONENT_TYPE_SHORT:
        source_type = VT_INT16;
        component_bytes = 2;
        break;
    case TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT:
        source_type = VT_UINT16;
        component_bytes = 2;
        break;
    case TINYGLTF_COMPONENT_TYPE_UNSIGNED_INT:
        source_type = VT_UINT32;
        component_bytes = 4;
        break;
    case TINYGLTF_COMPONENT_TYPE_FLOAT:
        source_type = VT_FLOAT32;
        component_bytes = 4;
        break;
    default:
        throw std::runtime_error("Unsupported native glTF component type");
    }
    constexpr auto destination_type = std::is_same_v<T, float> ? VT_FLOAT32 : VT_UINT32;
    if constexpr (!std::is_same_v<T, float>) {
        if (accessor.normalized ||
            (source_type != VT_UINT8 && source_type != VT_UINT16 && source_type != VT_UINT32))
            throw std::runtime_error("glTF integer IDs require non-normalized unsigned components");
    }
    unsigned rows = 0, columns = 1;
    switch (accessor.type) {
    case TINYGLTF_TYPE_SCALAR:
        rows = 1;
        break;
    case TINYGLTF_TYPE_VEC2:
        rows = 2;
        break;
    case TINYGLTF_TYPE_VEC3:
        rows = 3;
        break;
    case TINYGLTF_TYPE_VEC4:
        rows = 4;
        break;
    case TINYGLTF_TYPE_MAT2:
        rows = columns = 2;
        break;
    case TINYGLTF_TYPE_MAT3:
        rows = columns = 3;
        break;
    case TINYGLTF_TYPE_MAT4:
        rows = columns = 4;
        break;
    default:
        throw std::runtime_error("Unsupported native glTF accessor shape");
    }
    const unsigned components = rows * columns;
    const unsigned column_bytes =
        columns == 1 ? rows * component_bytes : (rows * component_bytes + 3) & ~3u;
    const unsigned element_bytes = column_bytes * columns;
    if (!accessor.count || accessor.count > (512 * 1024 * 1024) / sizeof(T) / components)
        throw std::runtime_error("Native glTF decoded accessor exceeds byte budget");
    NativeGltfValues<T> result{accessor.count, components,
                               std::vector<T>(accessor.count * components)};
    auto read = [&](int view_index, std::size_t offset, std::size_t count, T* destination) {
        if (view_index < 0 || std::size_t(view_index) >= model.bufferViews.size())
            throw std::runtime_error("Native glTF conversion has invalid bufferView");
        const auto& view = model.bufferViews[view_index];
        if (view.buffer < 0 || std::size_t(view.buffer) >= model.buffers.size())
            throw std::runtime_error("Native glTF conversion has invalid buffer");
        const auto& bytes = model.buffers[view.buffer].data;
        const auto stride = view.byteStride ? view.byteStride : element_bytes;
        if (stride < element_bytes || stride > UINT32_MAX || view.byteOffset > bytes.size() ||
            view.byteLength > bytes.size() - view.byteOffset || offset > view.byteLength ||
            element_bytes > view.byteLength - offset ||
            count - 1 > (view.byteLength - offset - element_bytes) / stride)
            throw std::runtime_error("Native glTF conversion range differs from admission");
        for (unsigned column = 0; column < columns; ++column) {
            GLTF::VertexDataConverter::WriteAttribs write;
            write.pSrc = bytes.data() + view.byteOffset + offset + column * column_bytes;
            write.SrcType = source_type;
            write.NumSrcComponents = rows;
            write.SrcElementStride = static_cast<Uint32>(stride);
            write.pDst = destination + column * rows;
            write.DstType = destination_type;
            write.NumDstComponents = rows;
            write.DstElementStride = components * sizeof(T);
            write.NumElements = static_cast<Uint32>(count);
            write.IsNormalized = accessor.normalized;
            if (!GLTF::VertexDataConverter::Write(write))
                throw std::runtime_error("Native glTF attribute conversion failed");
        }
    };
    if (accessor.bufferView >= 0)
        read(accessor.bufferView, accessor.byteOffset, accessor.count, result.values.data());
    if (accessor.sparse.isSparse) {
        const auto count = static_cast<std::size_t>(accessor.sparse.count);
        if (!count || count > accessor.count)
            throw std::runtime_error("Native glTF sparse count changed after admission");
        const auto& indices = accessor.sparse.indices;
        if (indices.bufferView < 0 || std::size_t(indices.bufferView) >= model.bufferViews.size())
            throw std::runtime_error("Invalid native glTF sparse index view");
        const auto& view = model.bufferViews[indices.bufferView];
        if (view.buffer < 0 || std::size_t(view.buffer) >= model.buffers.size())
            throw std::runtime_error("Invalid native glTF sparse index buffer");
        const auto& bytes = model.buffers[view.buffer].data;
        unsigned width = indices.componentType == TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE    ? 1
                         : indices.componentType == TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT ? 2
                         : indices.componentType == TINYGLTF_COMPONENT_TYPE_UNSIGNED_INT   ? 4
                                                                                           : 0;
        if (!width || view.byteOffset > bytes.size() ||
            view.byteLength > bytes.size() - view.byteOffset ||
            indices.byteOffset > view.byteLength ||
            count > (view.byteLength - indices.byteOffset) / width)
            throw std::runtime_error("Native glTF sparse index range differs from admission");
        std::vector<T> sparse(count * components);
        read(accessor.sparse.values.bufferView, accessor.sparse.values.byteOffset, count,
             sparse.data());
        std::uint32_t previous = 0;
        for (std::size_t i = 0; i < count; ++i) {
            std::uint32_t item = 0;
            for (unsigned b = 0; b < width; ++b)
                item |= std::uint32_t(bytes[view.byteOffset + indices.byteOffset + i * width + b])
                        << (8 * b);
            if (item >= accessor.count || (i && item <= previous))
                throw std::runtime_error("Native glTF sparse indices differ from admission");
            std::copy_n(sparse.data() + i * components, components,
                        result.values.data() + item * components);
            previous = item;
        }
    }
    return result;
}
} // namespace
NativeGltfDocument::NativeGltfDocument(GltfSourceBundle captured) : source_(std::move(captured)) {
    auto admitted = decode_gltf_draco(decode_gltf_meshopt(source_));
    meshes_ = admitted.document.value("meshes", nlohmann::json::array());
    (void)validate_gltf_accessors(admitted);
    validate_gltf_mesh_containers(admitted);
    validate_gltf_surfaces(admitted);
    (void)gltf_scene_metadata(admitted);
    hierarchy_ = validate_gltf_hierarchy(admitted);
    auto transport = std::move(admitted.document);
    std::map<std::string, std::span<const std::byte>> files;
    const std::string root = "/forge-captured/";
    for (std::size_t i = 0; i < admitted.buffers.size(); ++i) {
        const auto name = "buffer-" + std::to_string(i) + ".bin";
        transport.at("buffers").at(i)["uri"] = name;
        files.emplace(root + name, admitted.buffers[i].bytes());
    }
    // Native image metadata loading must not revisit source paths, including
    // its DecodeImages=false shortcut for recognized image filename extensions.
    // Encoded bytes and real MIME/provenance remain in source_.images by index.
    for (std::size_t i = 0; i < admitted.images.size(); ++i) {
        const auto name = "image-" + std::to_string(i) + ".forge-encoded";
        auto& image = transport.at("images").at(i);
        image.erase("bufferView");
        image["uri"] = name;
        files.emplace(root + name, admitted.images[i].encoded.bytes());
    }
    const auto encoded = transport.dump();
    files.emplace(root + "model.gltf", std::as_bytes(std::span(encoded)));
    auto locator = [](const char* path) {
        std::string value(path);
        std::replace(value.begin(), value.end(), '\\', '/');
        return value;
    };
    Diligent::GLTF::DocumentLoadInfo info;
    const auto virtual_source = root + "model.gltf";
    info.FileName = virtual_source.c_str();
    info.DecodeImages = false;
    info.FileExistsCallback = [&](const char* path) { return files.contains(locator(path)); };
    info.ReadWholeFileCallback = [&](const char* path, std::vector<unsigned char>& output,
                                     std::string& error) {
        const auto file = files.find(locator(path));
        if (file == files.end()) {
            error = "Native glTF loader requested a resource outside the captured manifest";
            return false;
        }
        const auto bytes = file->second;
        output.resize(bytes.size());
        std::transform(bytes.begin(), bytes.end(), output.begin(),
                       [](std::byte b) { return std::to_integer<unsigned char>(b); });
        ++captured_reads_;
        return true;
    };
    // The native constructor completes synchronously; no callbacks or borrowed
    // transport buffers escape. Never call its default filesystem fallback.
    native_ = std::make_unique<Diligent::GLTF::Document>(info);
    if (native_->GetModel().buffers.size() != admitted.buffers.size() ||
        native_->GetModel().images.size() != admitted.images.size())
        throw std::runtime_error("Native glTF source cardinality changed during parsing");
    images_ = std::move(admitted.images);
    expanded_ = expand_gltf_instances(*this);
    if (expanded_)
        hierarchy_ = validate_gltf_hierarchy(*expanded_);
}
NativeGltfDocument::~NativeGltfDocument() = default;
const tinygltf::Model& NativeGltfDocument::model() const { return native_->GetModel(); }
NativeGltfValues<float> NativeGltfDocument::floats(std::size_t accessor) const {
    return convert<float>(model(), accessor);
}
NativeGltfValues<std::uint32_t> NativeGltfDocument::unsigned_integers(std::size_t accessor) const {
    return convert<std::uint32_t>(model(), accessor);
}
} // namespace forge::asset_detail
