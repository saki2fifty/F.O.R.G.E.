#include "asset_bytes.hpp"
#include "asset_import_service.hpp"
#include "model_authoring.hpp"
#include "model_import_test.hpp"
#include "model_selection.hpp"
#include "runtime_package.hpp"
#include <fstream>
#include <iostream>
using namespace forge;
using namespace forge::asset_detail;
using namespace std::chrono_literals;
namespace {
using Json = nlohmann::json;
void check(bool ok, const char* why) {
    if (!ok)
        throw std::runtime_error(why);
}
void save(const std::filesystem::path& path, std::string_view text) {
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    check(bool(file.write(text.data(), std::streamsize(text.size()))) && bool(file.flush()),
          "Cannot write owned migration fixture");
}
AssetImportSidecar read_sidecar(const std::filesystem::path& path) {
    const auto bytes = read_bytes(path, max_asset_index_bytes);
    return AssetImportSidecar::parse(
        std::string_view(reinterpret_cast<const char*>(bytes.data()), bytes.size()));
}
std::vector<std::byte> snapshot(const std::filesystem::path& path) {
    return std::filesystem::exists(path) ? read_bytes(path, max_asset_index_bytes)
                                         : std::vector<std::byte>{};
}
void source(const std::filesystem::path& path, bool identical = false) {
    Json doc{{"asset", {{"version", "2.0"}, {"generator", "Changed original migration fixture"}}},
             {"nodes", Json::array({{{"name", "Part"},
                                     {"translation", {1, 0, 0}},
                                     {"rotation", {.20000002, .40000004, .40000004, .80000008}}},
                                    {{"name", "Part"},
                                     {"translation", {identical ? 1 : -1, 0, 0}},
                                     {"rotation", {.20000002, .40000004, .40000004, .80000008}}}})},
             {"scenes", Json::array({{{"nodes", {0, 1}}}})},
             {"scene", 0}};
    auto text = doc.dump();
    text.append((4 - text.size() % 4) % 4, ' ');
    std::string bytes;
    auto word = [&](std::uint32_t value) {
        for (unsigned shift = 0; shift < 32; shift += 8)
            bytes.push_back(char((value >> shift) & 255));
    };
    word(0x46546c67);
    word(2);
    word(std::uint32_t(20 + text.size()));
    word(std::uint32_t(text.size()));
    word(0x4e4f534a);
    save(path, bytes + text);
}
} // namespace
int main(int argc, char** argv) {
    try {
        check(argc == 5, "Need legacy fixture, scratch, worker-or-direct and converter");
        const auto fixture = std::filesystem::absolute(argv[1]);
        const auto base = std::filesystem::absolute(argv[2]);
        const auto converter = std::filesystem::absolute(argv[4]);
        auto importer = model_importer(std::filesystem::absolute(argv[3]), converter);
        if (std::string_view(argv[3]) == "--direct")
            importer = std::make_shared<test::DirectModelImporter>(importer, converter);
        auto registry = std::make_shared<AssetImporterRegistry>();
        registry->add(importer);
        registry->seal();
        const auto locator = std::filesystem::path("Assets/Parts.glb");
        for (const auto* mode :
             {"migrate", "rollback", "changed", "settings", "missing", "tampered", "structure",
              "duplicate-v2", "missing-sidecar", "corrupt"}) {
            const auto root = base / mode / AssetId::generate().str();
            std::filesystem::create_directories(root);
            std::filesystem::copy(fixture, root, std::filesystem::copy_options::recursive);
            const auto sidecar_path = root / AssetPublisher::sidecar_path(locator);
            auto previous = read_sidecar(sidecar_path);
            auto catalog = AssetCatalog::open_project(root);
            const auto owner = previous.identity.owner;
            const auto selected = load_model_selection(root, catalog, owner);
            check(selected.index.version == 4 &&
                      previous.identity.evidence_schema == "forge.gltf-model.v1",
                  "Golden fixture is no longer the real legacy format");
            auto expected = selected.bindings;
            const auto tombstone = AssetId::generate();
            previous.unknown["vendor-root"] = {{"preserve", true}};
            previous.identity.unknown["vendor-map"] = {"opaque", "root"};
            previous.identity.entries.front().unknown["vendor-entry"] = {"opaque", "member"};
            previous.identity.entries.push_back(
                {tombstone,
                 "member/retained",
                 "model_node",
                 "Removed",
                 {"retained-exporter", std::string(64, 'a'), std::string(64, 'b')},
                 true,
                 {{"vendor-tombstone", {"preserve", true}}}});
            AssetRecord removed{tombstone, "model_node", locator, 1, {}};
            removed.subasset = AssetSubasset{owner, "member/retained", true};
            catalog.add(std::move(removed));
            catalog.save(root / "forge.assets.json");
            if (std::string_view(mode) == "changed")
                source(root / locator);
            if (std::string_view(mode) == "tampered")
                previous.identity.entries.front().evidence.content_digest = std::string(64, 'c');
            if (std::string_view(mode) == "missing")
                std::filesystem::remove_all(root / ".forge/cache/derived" / selected.revision);
            if (std::string_view(mode) == "corrupt")
                save(root / ".forge/cache/derived" / selected.revision / "manifest.json",
                     "{invalid");
            save(sidecar_path, previous.document().dump());
            if (std::string_view(mode) == "missing-sidecar")
                std::filesystem::remove(sidecar_path);
            if (std::string_view(mode) == "duplicate-v2") {
                std::filesystem::remove(root / "forge.assets.json");
                std::filesystem::remove(sidecar_path);
                source(root / locator, true);
            }
            const auto before_catalog =
                std::filesystem::exists(root / "forge.assets.json")
                    ? read_bytes(root / "forge.assets.json", max_asset_index_bytes)
                    : std::vector<std::byte>{};
            const auto before_sidecar = snapshot(sidecar_path);
            auto lease = std::make_shared<ProjectLease>(root);
            AssetImportService service(lease, registry, {"windows", "d3d12", "desktop"}, 1);
            auto run = [&](std::vector<SubassetIdentityDecision> choices = {}, bool reject = false,
                           bool tool_change = false) {
                auto draft = service.prepare(locator);
                if (std::string_view(mode) == "settings")
                    draft.request.settings = draft.importer->settings().edit(draft.request.settings,
                                                                             "normals", "preserve");
                service.submit(
                    draft,
                    [&](auto& c, const auto& input, const auto& old) {
                        auto plan = input;
                        if (tool_change) {
                            c.input.tool_revisions["fixture-rebuild"] = std::string(64, 'd');
                            plan.input = c.input;
                        }
                        if (std::string_view(mode) == "structure") {
                            c.input.tool_revisions["fixture-structure"] = std::string(64, 'e');
                            plan.input = c.input;
                            for (auto& file : c.files)
                                if (file.name == "model.json") {
                                    auto index = decode_model_bundle_index(file.bytes);
                                    std::swap(index.members[0].node, index.members[1].node);
                                    file.bytes = encode_model_bundle_index(index);
                                }
                        }
                        c.sidecar.build_inputs = c.input.document();
                        prepare_model_publication(c, plan, old, choices);
                    },
                    [reject](const auto&, const auto&) {
                        check(!reject, "Deliberate migration compatibility rejection");
                    });
                check(service.wait_idle(60s), "Migration test import timed out");
                auto receipts = service.poll();
                check(receipts.size() == 1, "Missing migration receipt");
                return std::move(receipts.front());
            };
            auto result = run({}, std::string_view(mode) == "rollback");
            const bool positive =
                std::string_view(mode) == "migrate" || std::string_view(mode) == "duplicate-v2";
            if (!positive) {
                check(!result.published &&
                          read_bytes(root / "forge.assets.json", max_asset_index_bytes) ==
                              before_catalog &&
                          snapshot(sidecar_path) == before_sidecar,
                      "Rejected migration changed last-good metadata");
                if (std::string_view(mode) == "tampered" || std::string_view(mode) == "rollback" ||
                    std::string_view(mode) == "missing-sidecar") {
                    check(result.diagnostic.find(std::string_view(mode) == "tampered"
                                                     ? "sidecar evidence"
                                                 : std::string_view(mode) == "missing-sidecar"
                                                     ? "sidecar is missing"
                                                     : "Deliberate migration") != std::string::npos,
                          "Negative migration fixture failed before its intended guard");
                    continue;
                }
                check(!result.identity_conflicts.empty(), "Unproven migration guessed member IDs");
                std::vector<SubassetIdentityDecision> choices;
                for (const auto& [address, id] : expected)
                    choices.push_back({address, id});
                auto partial = choices;
                partial.pop_back();
                check(!run(partial).published, "Incomplete migration review was accepted");
                result = run(choices);
            }
            check(result.published, result.diagnostic.c_str());
            const auto new_owner =
                result.publication->catalog.resolve(owner, "model").state == AssetState::Available
                    ? owner
                    : service.prepare(locator).request.asset;
            auto current = load_model_selection(root, result.publication->catalog, new_owner);
            check(current.index.version == 5 &&
                      current.index.evidence_schema == "forge.gltf-model.v2",
                  "Migration did not publish the declared evidence version");
            if (std::string_view(mode) == "duplicate-v2")
                expected = current.bindings;
            check(current.bindings == expected, "Migration changed logical member identities");
            if (std::string_view(mode) == "migrate") {
                std::ifstream oracle_file(fixture / "expected-v2.json");
                const auto oracle = Json::parse(oracle_file);
                for (const auto& row : oracle) {
                    const auto& member =
                        current.member(current.bindings.at(row.at("address").get<std::string>()));
                    check(member.identity.evidence.content_digest ==
                                  row.at("content_digest").get<std::string>() &&
                              member.identity.evidence.semantic_digest ==
                                  row.at("semantic_digest").get<std::string>(),
                          "Platform source evidence differs from the independent JSON/hash oracle");
                }
            }
            if (std::string_view(mode) != "duplicate-v2") {
                const auto after = read_sidecar(sidecar_path);
                check(after.identity.owner == owner && after.unknown == previous.unknown &&
                          after.identity.unknown == previous.identity.unknown,
                      "Migration changed container identity or root opaque metadata");
                for (const auto& entry : previous.identity.entries) {
                    const auto found =
                        std::find_if(after.identity.entries.begin(), after.identity.entries.end(),
                                     [&](const auto& row) { return row.id == entry.id; });
                    check(found != after.identity.entries.end() && found->key == entry.key &&
                              found->removed == entry.removed && found->unknown == entry.unknown &&
                              found->evidence.exporter_key == entry.evidence.exporter_key,
                          "Migration lost a durable key, tombstone or opaque member data");
                }
            }
            auto repeat = run({}, false, std::string_view(mode) == "duplicate-v2");
            if (!repeat.published)
                throw std::runtime_error(std::string("Repeat rejected in ") + mode + ": " +
                                         repeat.diagnostic);
            check(load_model_selection(root, repeat.publication->catalog, new_owner).bindings ==
                      expected,
                  "Reimport/tool change lost unchanged source member identities");
            if (std::string_view(mode) == "migrate") {
                const auto exported = base / ("offline-" + AssetId::generate().str());
                const std::array roots{owner};
                (void)package_runtime_content(root, exported, roots, {"windows", "d3d12"});
                const auto portable = open_runtime_content(exported, {"windows", "d3d12"});
                check(!std::filesystem::exists(exported / locator) &&
                          load_model_selection(exported, portable, owner).bindings == expected,
                      "Migrated cooked model lost source-independent runtime bindings");
            }
        }
        std::cout << "Model evidence migration, review, failure retention and source-free "
                     "consumption passed\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
