#include "asset_source_document.hpp"
#include <cstdint>
#include <iostream>
#include <limits>
using namespace forge;
namespace {
using Json = nlohmann::json;
void require(bool value, const char* why) {
    if (!value)
        throw std::runtime_error(why);
}
template <class F> void reject(F&& action) {
    try {
        action();
    } catch (const std::exception&) {
        return;
    }
    throw std::runtime_error("Expected source-document rejection");
}
Json initial(AssetId id) {
    return {{"asset", id},
            {"value", 0},
            {"opaque/~root", {{"wide", UINT64_MAX - 7}, {"items", {true, nullptr, "keep"}}}}};
}
void validate(const Json& value) {
    require(bool(value.at("asset").get<AssetId>()), "Invalid source identity");
    require(value.at("value").is_number_integer(), "Expected integer source value");
    const auto number = value.at("value").get<int>();
    require(number >= 0 && number <= 1000, "Source value outside test profile");
}
Json parse(std::span<const std::byte> bytes) {
    return Json::parse(reinterpret_cast<const char*>(bytes.data()),
                       reinterpret_cast<const char*>(bytes.data() + bytes.size()));
}
std::string read(const std::filesystem::path& path) {
    const auto bytes = asset_detail::read_bytes(path, 1024 * 1024);
    return {reinterpret_cast<const char*>(bytes.data()), bytes.size()};
}
// An actual non-JSON value consumer: copies share immutable data; mutation
// prepares a detached value. It is a test fixture, not a tilemap source format.
class ImmutableValue {
    std::shared_ptr<const Json> root_ = std::make_shared<const Json>(Json::object());

  public:
    ImmutableValue() = default;
    explicit ImmutableValue(Json value) : root_(std::make_shared<const Json>(std::move(value))) {}
    const Json& json() const { return *root_; }
    std::string dump(int indent = -1) const { return root_->dump(indent); }
    void set(const char* key, Json value) {
        auto candidate = *root_;
        candidate[key] = std::move(value);
        root_ = std::make_shared<const Json>(std::move(candidate));
    }
    friend bool operator==(const ImmutableValue& a, const ImmutableValue& b) {
        return a.root_ == b.root_ || *a.root_ == *b.root_;
    }
};
struct TypedSource {
    ImmutableValue document;
    AssetId asset() const { return document.json().at("asset").get<AssetId>(); }
    void validate() const { ::validate(document.json()); }
    static TypedSource parse(std::span<const std::byte> bytes) {
        TypedSource source{ImmutableValue(::parse(bytes))};
        source.validate();
        return source;
    }
};
struct TypedTraits {
    using Source = TypedSource;
    static constexpr auto byte_limit = 1024 * 1024;
    static constexpr auto suffix = ".typed-document.json";
    static constexpr auto asset_type = "document_test_only";
    static inline std::size_t weight = 1024 * 1024;
    static Source create(AssetId id) { return {ImmutableValue(initial(id))}; }
    static std::size_t history_weight(const Source&, const Source&) { return weight; }
};
struct JsonSource {
    Json document;
    AssetId asset() const { return document.at("asset").get<AssetId>(); }
    void validate() const { ::validate(document); }
    static JsonSource parse(std::span<const std::byte> bytes) {
        JsonSource source{::parse(bytes)};
        source.validate();
        return source;
    }
};
struct JsonTraits {
    using Source = JsonSource;
    static constexpr auto byte_limit = 1024 * 1024;
    static constexpr auto suffix = ".json-document.json";
    static constexpr auto asset_type = "document_test_only";
    static Source create(AssetId id) { return {initial(id)}; }
};
using TypedDocument = AssetSourceDocument<TypedTraits>;
void typed(const std::filesystem::path& root) {
    auto lease = std::make_shared<ProjectLease>(root);
    const auto path = root / "Values.typed-document.json";
    auto document = TypedDocument::create(lease, path.filename());
    const auto original = document->source().document;
    const auto id = document->source().asset();
    const auto disk = read(path);
    require(!document->dirty() && !document->can_undo(), "New typed source is not clean");
    for (int i = 1; i <= 16; ++i)
        document->edit_source(document->revision(), "Typed value",
                              [i](auto& source) { source.document.set("value", i); });
    require(original.json().at("value") == 0, "Candidate aliased the original immutable source");
    require(document->source().document.json().at("opaque/~root") ==
                original.json().at("opaque/~root"),
            "Typed edits changed opaque values or wide integers");
    require(read(path) == disk, "Typed edit wrote source without Save");
    unsigned undos = 0;
    while (document->can_undo()) {
        document->undo();
        ++undos;
    }
    require(undos == 8 && document->source().document.json().at("value") == 8,
            "Typed source ignored its bounded history weight");
    unsigned redos = 0;
    while (document->can_redo()) {
        document->redo();
        ++redos;
    }
    require(redos == 8 && document->source().document.json().at("value") == 16,
            "Typed source Redo did not restore the edited value");
    document->undo();
    const auto revision = document->revision();
    document->edit_source(revision, "Equivalent detached value",
                          [](auto& source) { source.document.set("value", 15); });
    require(document->revision() == revision && document->can_redo(),
            "Equivalent typed value created history or lost Redo");
    bool called = false;
    reject(
        [&] { document->edit_source(revision - 1, "Stale edit", [&](auto&) { called = true; }); });
    require(!called, "Stale edit invoked its mutation");
    reject([&] {
        document->edit_source(revision, "Wrong identity", [](auto& source) {
            source.document.set("asset", AssetId::generate());
        });
    });
    reject([&] {
        document->edit_source(revision, "Invalid value",
                              [](auto& source) { source.document.set("value", 1001); });
    });
    reject([&] { document->edit_source(revision, "Empty mutation", {}); });
    reject([&] {
        document->edit_source(revision, "Throwing mutation", [](auto& source) {
            source.document.set("value", 99);
            throw std::runtime_error("Cancelled detached mutation");
        });
    });
    TypedTraits::weight = 0;
    reject([&] {
        document->edit_source(revision, "Invalid history weight",
                              [](auto& source) { source.document.set("value", 99); });
    });
    reject([&] { document->undo(); });
    reject([&] { document->redo(); });
    TypedTraits::weight = 1024 * 1024;
    require(document->revision() == revision && document->can_redo() &&
                document->source().asset() == id &&
                document->source().document.json().at("value") == 15 && read(path) == disk,
            "Rejected edits changed active source, identity, revision, Redo or disk");
    bool wrong_thread_rejected = false;
    std::thread worker([&] {
        try {
            document->edit_source(revision, "Other thread",
                                  [](auto& source) { source.document.set("value", 9); });
        } catch (const std::exception&) {
            wrong_thread_rejected = true;
        }
    });
    worker.join();
    require(wrong_thread_rejected && document->revision() == revision,
            "Typed edit bypassed its owner thread");
    document->redo();
    document->save();
    require(!document->dirty() && read(path) != disk, "Typed Save did not adopt its saved value");
    const auto saved = document->source().document;
    document->undo();
    require(document->dirty(), "Undo after Save lost the saved-value boundary");
    document->redo();
    require(!document->dirty(), "Redo to saved source remained dirty");
    TypedDocument reopened(lease, path.filename());
    require(reopened.source().document == saved && reopened.source().asset() == id,
            "Typed source did not reopen with exact known/opaque identity data");
    document->edit_source(document->revision(), "Unsaved draft",
                          [](auto& source) { source.document.set("value", 17); });
    const auto saved_disk = read(path);
    atomic_write(path, saved_disk + " ");
    reject([&] { document->save(); });
    require(document->dirty() && document->source().document.json().at("value") == 17 &&
                read(path) == saved_disk + " ",
            "External baseline rejection lost the draft or overwrote the file");
    atomic_write(path, saved_disk);
    document->save();
    require(!document->dirty(), "Typed Save failed after restoring the exact baseline");
    TypedTraits::weight = std::numeric_limits<std::size_t>::max();
    document->edit_source(document->revision(), "Oversized weight",
                          [](auto& source) { source.document.set("value", 18); });
    require(!document->can_undo(), "Oversized history weight wrapped its bound");
    TypedTraits::weight = 1024 * 1024;
}
void legacy(const std::filesystem::path& root) {
    auto lease = std::make_shared<ProjectLease>(root);
    using Document = AssetSourceDocument<JsonTraits>;
    auto document = Document::create(lease, "Values.json-document.json");
    const auto original = document->source().document;
    for (int i = 1; i <= 80; ++i)
        document->edit(document->revision(), "JSON value",
                       [i](auto& source) { source["value"] = i; });
    unsigned undos = 0;
    while (document->can_undo()) {
        document->undo();
        ++undos;
    }
    require(undos == 64 && document->source().document.at("value") == 16,
            "Existing JSON source lost its default snapshot/64-entry behavior");
    require(document->source().document.at("opaque/~root") == original.at("opaque/~root"),
            "Existing JSON source lost opaque fields");
    reject([&] { document->edit(document->revision(), "Empty JSON mutation", {}); });
    document->save();
    Document reopened(lease, "Values.json-document.json");
    require(reopened.source().document == document->source().document,
            "Existing JSON source no longer saves/reopens exactly");
}
} // namespace
int main(int argc, char** argv) {
    try {
        require(argc == 2, "Expected a dedicated source-document test directory");
        const auto root = std::filesystem::path(argv[1]) / AssetId::generate().str();
        std::filesystem::create_directories(root / "typed");
        std::filesystem::create_directories(root / "legacy");
        typed(root / "typed");
        legacy(root / "legacy");
        std::filesystem::remove_all(root);
        std::cout << "Source document typed/JSON lifecycle, identity, history, Save and rejection "
                     "passed\n";
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
