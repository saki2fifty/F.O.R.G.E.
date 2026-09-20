#include "gltf_snapshot.hpp"
#include "asset_bytes.hpp"
#include "bounded_json.hpp"
#include <algorithm>
#include <set>
namespace forge::asset_detail {
namespace {
using Json = nlohmann::json;
using Bytes = std::vector<std::byte>;
constexpr std::size_t index_limit = 16 * 1024 * 1024;
void require(bool ok, const char* why) {
    if (!ok)
        throw std::runtime_error(why);
}
void cancelled(std::stop_token stop) { require(!stop.stop_requested(), "glTF snapshot cancelled"); }
std::size_t number(const Json& value, std::size_t max) {
    require(value.is_number_integer() &&
                (value.is_number_unsigned() || value.get<std::int64_t>() >= 0),
            "Invalid glTF snapshot integer");
    const auto result = value.get<std::uint64_t>();
    require(result <= max, "glTF snapshot count/length exceeds bounds");
    return static_cast<std::size_t>(result);
}
std::string text(const Json& j, std::size_t max, bool empty = false) {
    require(j.is_string(), "Invalid glTF snapshot string");
    const auto& s = j.get_ref<const std::string&>();
    require((empty || !s.empty()) && s.size() <= max && s.find('\0') == std::string::npos,
            "glTF snapshot string exceeds bounds");
    return s;
}
const Json& array(const Json& value, std::size_t max) {
    require(value.is_array() && value.size() <= max, "glTF snapshot array exceeds bounds");
    return value;
}
void budgets(GltfSourceLimits l) {
    require(l.file_bytes && l.file_bytes <= 1024ull * 1024 * 1024 &&
                l.total_bytes >= l.file_bytes && l.total_bytes <= 2ull * 1024 * 1024 * 1024 &&
                l.json_bytes && l.json_bytes <= l.file_bytes && l.source_files &&
                l.source_files <= 4096 && l.array_entries && l.array_entries <= 1000000,
            "Invalid glTF snapshot limits");
}
Bytes json_bytes(const Json& value, std::size_t maximum) {
    const auto s = value.dump();
    require(s.size() <= maximum, "glTF snapshot JSON exceeds budget");
    const auto b = std::as_bytes(std::span(s));
    return {b.begin(), b.end()};
}
void shape(const GltfSourceBundle& source, GltfSourceLimits limits) {
    budgets(limits);
    require(source.document.is_object() && source.document.at("asset").at("version") == "2.0" &&
                valid_content_digest(source.source_digest),
            "Invalid glTF snapshot source");
    require(source.source == ProjectPaths::normalize(source.source) &&
                path_utf8(source.source).size() <= 4096,
            "Invalid glTF snapshot locator");
    for (const auto [key, count] :
         {std::pair{"buffers", source.buffers.size()}, std::pair{"images", source.images.size()}}) {
        require(count <= limits.array_entries, "glTF snapshot resource count exceeds bounds");
        if (source.document.contains(key))
            require(array(source.document.at(key), limits.array_entries).size() == count,
                    "glTF snapshot resource count disagrees with document");
        else
            require(count == 0, "glTF snapshot resources missing document entries");
    }
    require(source.captured_bytes <= limits.total_bytes, "glTF source capture exceeds bounds");
    for (std::size_t i = 0; i < source.buffers.size(); ++i) {
        const auto& b = source.buffers[i];
        require(b.length && b.length == number(source.document.at("buffers").at(i).at("byteLength"),
                                               limits.file_bytes),
                "glTF snapshot buffer length disagrees with source");
        if (!b.storage) {
            const auto required = source.document.value("extensionsRequired", Json::array());
            require(b.offset == 0 && required.is_array() &&
                        std::find(required.begin(), required.end(), "EXT_meshopt_compression") !=
                            required.end(),
                    "glTF snapshot placeholder requires meshopt compression");
        }
    }
    for (std::size_t i = 0; i < source.images.size(); ++i) {
        const auto& image = source.images[i];
        const auto& definition = source.document.at("images").at(i);
        (void)text(image.mime_type, 128, true);
        require(image.encoded.length != 0, "Empty glTF snapshot image");
        if (definition.contains("bufferView")) {
            const auto& views = array(source.document.at("bufferViews"), limits.array_entries);
            const auto view_index = number(definition.at("bufferView"), views.size());
            require(view_index < views.size(), "Invalid snapshot image view");
            const auto& view = views[view_index];
            const auto parent = number(view.at("buffer"), source.buffers.size());
            require(parent < source.buffers.size(), "Invalid snapshot image buffer");
            const auto& b = source.buffers[parent];
            const auto offset = number(view.value("byteOffset", Json(0u)), b.length);
            const auto length = number(view.at("byteLength"), b.length - offset);
            require(image.encoded.storage == b.storage &&
                        image.encoded.offset == b.offset + offset && image.encoded.length == length,
                    "glTF snapshot image disagrees with source view");
        } else
            require(bool(image.encoded.storage),
                    "URI image cannot use a missing buffer placeholder");
    }
    for (const auto& s : source.optional_extensions)
        (void)text(s, 256);
    for (const auto& s : source.diagnostics)
        (void)text(s, 8192);
    require(source.dependencies.size() <= limits.array_entries &&
                source.diagnostics.size() <= limits.array_entries &&
                source.optional_extensions.size() <= limits.array_entries,
            "glTF snapshot provenance exceeds bounds");
}
} // namespace
std::vector<ArtifactFile> encode_gltf_snapshot(const GltfSourceBundle& source,
                                               GltfSourceLimits limits, std::stop_token stop) {
    cancelled(stop);
    shape(source, limits);
    std::vector<ArtifactFile> files;
    files.push_back({"source.json", json_bytes(source.document, limits.json_bytes)});
    std::size_t total = files.front().bytes.size();
    Json blobs = Json::array();
    std::map<const Bytes*, std::size_t> stored;
    auto range = [&](const GltfByteRange& r) {
        require(r.length <= limits.file_bytes, "glTF snapshot range exceeds bounds");
        if (!r.storage) {
            require(r.offset <= limits.file_bytes && r.length &&
                        r.length <= limits.file_bytes - r.offset,
                    "Invalid glTF snapshot missing storage");
            return Json{{"blob", nullptr}, {"offset", r.offset}, {"length", r.length}};
        }
        (void)r.bytes();
        auto found = stored.find(r.storage.get());
        if (found == stored.end()) {
            cancelled(stop);
            require(blobs.size() < limits.source_files && r.storage->size() <= limits.file_bytes &&
                        r.storage->size() <= limits.total_bytes - total,
                    "glTF snapshot backing storage exceeds budget");
            total += r.storage->size();
            const auto name = "blob-" + std::to_string(blobs.size()) + ".bin";
            found = stored.emplace(r.storage.get(), blobs.size()).first;
            blobs.push_back({{"file", name},
                             {"bytes", r.storage->size()},
                             {"sha256", content_digest(*r.storage)}});
            files.push_back({name, *r.storage});
        }
        return Json{{"blob", found->second}, {"offset", r.offset}, {"length", r.length}};
    };
    Json buffers = Json::array(), images = Json::array(), dependencies = Json::array();
    for (const auto& b : source.buffers)
        buffers.push_back(range(b));
    for (const auto& i : source.images)
        images.push_back({{"encoded", range(i.encoded)}, {"mime", i.mime_type}});
    for (const auto& d : source.dependencies) {
        require(d.source == ProjectPaths::normalize(d.source) && valid_content_digest(d.revision),
                "Invalid glTF snapshot source dependency");
        dependencies.push_back(
            {{"source", path_utf8(d.source)}, {"role", d.role}, {"sha256", d.revision}});
    }
    Json index{{"format", "forge.gltf-snapshot"},
               {"version", 1},
               {"source", path_utf8(source.source)},
               {"source_digest", source.source_digest},
               {"document_digest", content_digest(files.front().bytes)},
               {"binary_container", source.binary_container},
               {"captured_bytes", source.captured_bytes},
               {"blobs", blobs},
               {"buffers", buffers},
               {"images", images},
               {"dependencies", dependencies},
               {"optional_extensions", source.optional_extensions},
               {"diagnostics", source.diagnostics}};
    auto encoded = json_bytes(index, std::min(index_limit, limits.file_bytes));
    require(encoded.size() <= limits.total_bytes - total,
            "glTF snapshot index exceeds total budget");
    files.push_back({"capture.json", std::move(encoded)});
    cancelled(stop);
    return files;
}
GltfSourceBundle decode_gltf_snapshot(std::vector<ArtifactFile> files, GltfSourceLimits limits,
                                      std::stop_token stop) {
    cancelled(stop);
    budgets(limits);
    require(files.size() >= 2 && files.size() - 2 <= limits.source_files,
            "glTF snapshot file count exceeds bounds");
    std::map<std::string, std::size_t> lookup;
    std::size_t total = 0;
    for (std::size_t i = 0; i < files.size(); ++i) {
        require(files[i].bytes.size() <= limits.file_bytes &&
                    files[i].bytes.size() <= limits.total_bytes - total &&
                    lookup.emplace(files[i].name, i).second,
                "glTF snapshot file budget/duplicate name");
        total += files[i].bytes.size();
    }
    auto take = [&](const std::string& name) -> Bytes& {
        const auto at = lookup.find(name);
        require(at != lookup.end(), "Missing glTF snapshot file");
        auto& result = files[at->second].bytes;
        lookup.erase(at);
        return result;
    };
    const auto index = parse_bounded_json(take("capture.json"), index_limit);
    require(index.at("format") == "forge.gltf-snapshot" && number(index.at("version"), 1) == 1,
            "Unsupported glTF snapshot format");
    auto& document = take("source.json");
    require(content_digest(document) == text(index.at("document_digest"), 64),
            "glTF snapshot document digest changed");
    GltfSourceBundle source;
    source.document = parse_bounded_json(document, limits.json_bytes);
    source.source = std::filesystem::u8path(text(index.at("source"), 4096));
    source.source_digest = text(index.at("source_digest"), 64);
    source.binary_container = index.at("binary_container").get<bool>();
    source.captured_bytes = number(index.at("captured_bytes"), limits.total_bytes);
    std::vector<std::shared_ptr<const Bytes>> storage;
    for (const auto& blob : array(index.at("blobs"), limits.source_files)) {
        cancelled(stop);
        const auto name = "blob-" + std::to_string(storage.size()) + ".bin";
        require(blob.at("file") == name, "Noncanonical glTF snapshot blob name");
        auto& bytes = take(name);
        require(bytes.size() == number(blob.at("bytes"), limits.file_bytes) &&
                    content_digest(bytes) == text(blob.at("sha256"), 64),
                "glTF snapshot blob digest/length changed");
        storage.push_back(std::make_shared<const Bytes>(std::move(bytes)));
    }
    require(lookup.empty(), "Unexpected glTF snapshot file");
    std::set<std::size_t> used;
    auto range = [&](const Json& j) {
        GltfByteRange r;
        r.offset = number(j.at("offset"), limits.file_bytes);
        r.length = number(j.at("length"), limits.file_bytes);
        if (j.at("blob").is_null()) {
            require(r.length && r.length <= limits.file_bytes - r.offset,
                    "Invalid glTF snapshot placeholder");
        } else {
            const auto at = number(j.at("blob"), storage.size());
            require(at < storage.size(), "Invalid glTF snapshot blob index");
            used.insert(at);
            r.storage = storage[at];
            (void)r.bytes();
        }
        return r;
    };
    for (const auto& b : array(index.at("buffers"), limits.array_entries))
        source.buffers.push_back(range(b));
    for (const auto& i : array(index.at("images"), limits.array_entries))
        source.images.push_back({range(i.at("encoded")), text(i.at("mime"), 128, true)});
    require(used.size() == storage.size(), "Unreferenced glTF snapshot blob");
    for (const auto& d : array(index.at("dependencies"), limits.array_entries)) {
        auto path = std::filesystem::u8path(text(d.at("source"), 4096));
        const auto digest = text(d.at("sha256"), 64);
        require(path == ProjectPaths::normalize(path) && valid_content_digest(digest),
                "Invalid glTF snapshot dependency");
        source.dependencies.push_back({std::move(path), text(d.at("role"), 256), digest});
    }
    for (const auto& s : array(index.at("optional_extensions"), limits.array_entries))
        source.optional_extensions.push_back(text(s, 256));
    for (const auto& s : array(index.at("diagnostics"), limits.array_entries))
        source.diagnostics.push_back(text(s, 8192));
    shape(source, limits);
    cancelled(stop);
    return source;
}
} // namespace forge::asset_detail
