#include "asset_bytes.hpp"
#include "model_render_resource.hpp"
#include "texture_importer.hpp"
#include <forge/asset_publication.hpp>
#include <forge/scene.hpp>
#include <forge/texture_bundle.hpp>
#include <forge/texture_resource.hpp>
#include <fstream>
#include <iostream>
#include <source_location>
using namespace forge;
using namespace forge::asset_detail;
using namespace std::chrono_literals;
namespace {
void require(bool ok, const char* why) {
    if (!ok)
        throw std::runtime_error(why);
}
template <class F> void rejects(F f, std::source_location where = std::source_location::current()) {
    try {
        f();
    } catch (const std::exception&) {
        return;
    }
    throw std::runtime_error("Invalid texture pipeline accepted at line " +
                             std::to_string(where.line()));
}
void write(const std::filesystem::path& path, std::span<const std::byte> bytes) {
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    require(bool(file.write(reinterpret_cast<const char*>(bytes.data()),
                            std::streamsize(bytes.size()))) &&
                bool(file.flush()),
            "Fixture write failed");
}
std::vector<std::byte> image() {
    std::vector<std::byte> bytes(18);
    bytes[2] = std::byte{2};
    bytes[12] = std::byte{2};
    bytes[14] = std::byte{2};
    bytes[16] = std::byte{24};
    bytes[17] = std::byte{32};
    for (unsigned i = 0; i < 4; ++i) {
        const auto channel = std::byte(i % 2 ? 0 : 255);
        bytes.insert(bytes.end(), {channel, channel, channel});
    }
    return bytes;
}
std::vector<std::byte> dds() {
    // Owned 2x2 legacy RGBA fixture with an explicit supplied 1x1 mip.
    std::vector<std::byte> bytes(128);
    auto put = [&](unsigned at, unsigned value) {
        for (unsigned i = 0; i < 4; ++i)
            bytes.at(at + i) = std::byte((value >> (8 * i)) & 255);
    };
    put(0, 0x20534444);
    put(4, 124);
    put(8, 0x21007);
    put(12, 2);
    put(16, 2);
    put(28, 2);
    put(76, 32);
    put(80, 0x41);
    put(88, 32);
    put(92, 255);
    put(96, 0xff00);
    put(100, 0xff0000);
    put(104, 0xff000000);
    put(108, 0x401008);
    bytes.insert(bytes.end(), 16, std::byte{255});
    bytes.insert(bytes.end(), {std::byte{12}, std::byte{34}, std::byte{56}, std::byte{255}});
    return bytes;
}
std::vector<ArtifactFile> cook(const AssetImporter& importer, const AssetImportRequest& request,
                               const AssetImportPlan& plan, bool direct) {
    if (!direct)
        return importer.import_and_cook(request, plan, {}, {});
    ImportProcessRequest input{
        {{"recipe", importer.descriptor().id},
         {"revision", texture_recipe_revision()},
         {"settings", request.settings},
         {"kind", plan.data.at("source_kind")},
         {"extension", plan.data.at("extension")},
         {"source_digest", plan.input.source_digest},
         {"backend", request.target.backend}},
        {{"source.bin", read_bytes(request.project / request.source, 256 * 1024 * 1024)}}};
    return execute_texture_recipe(input);
}
void transport_admission(const std::filesystem::path& root) {
    const auto staging = root / "transport";
    std::filesystem::create_directories(staging / "input");
    std::filesystem::create_directory(staging / "output");
    const auto bytes = image();
    write(staging / "input/source.bin", bytes);
    const nlohmann::json valid{
        {"format", "forge.import-worker"},
        {"version", 1},
        {"payload", nlohmann::json::object()},
        {"inputs", nlohmann::json::array({{{"name", "source.bin"},
                                           {"bytes", bytes.size()},
                                           {"sha256", content_digest(bytes)}}})}};
    auto save = [&](const auto& document) {
        const auto text = document.dump();
        write(staging / "request.json", std::as_bytes(std::span(text)));
    };
    save(valid);
    require(read_import_process_request(staging, texture_worker_limits()).inputs[0].bytes == bytes,
            "Snapshot transport changed input bytes");
    auto malformed = valid;
    malformed["inputs"][0]["sha256"] = std::string(64, 'a');
    save(malformed);
    rejects([&] { read_import_process_request(staging, texture_worker_limits()); });
    malformed = valid;
    malformed["inputs"][0]["bytes"] = 1ull << 32;
    save(malformed);
    rejects([&] { read_import_process_request(staging, texture_worker_limits()); });
    malformed = valid;
    malformed["inputs"].push_back(malformed["inputs"][0]);
    save(malformed);
    rejects([&] { read_import_process_request(staging, texture_worker_limits()); });
    malformed = valid;
    malformed["version"] = 2;
    save(malformed);
    rejects([&] { read_import_process_request(staging, texture_worker_limits()); });
    for (const auto* name : {"../source.bin", "con.bin", "source.bin.", "SOURCE.bin"}) {
        malformed = valid;
        malformed["inputs"][0]["name"] = name;
        save(malformed);
        rejects([&] { read_import_process_request(staging, texture_worker_limits()); });
    }
    save(valid);
    write(staging / "input/unexpected.bin", bytes);
    rejects([&] { read_import_process_request(staging, texture_worker_limits()); });
    std::filesystem::remove(staging / "input/unexpected.bin");
    write_import_process_result(staging, {{"compressed.bin", bytes}}, texture_worker_limits());
    const auto manifest = read_bytes(staging / "output/manifest.json", 16384);
    rejects([&] {
        write_import_process_result(staging, {{"compressed.bin", bytes}}, texture_worker_limits());
    });
    require(read_bytes(staging / "output/manifest.json", 16384) == manifest,
            "Second result overwrote completion marker");
}
} // namespace
int main(int argc, char** argv) {
    try {
        require(argc == 3 || argc == 4, "Need --direct root or --worker executable root");
        const bool direct = std::string_view(argv[1]) == "--direct";
        require((direct && argc == 3) ||
                    (!direct && argc == 4 && std::string_view(argv[1]) == "--worker"),
                "Invalid pipeline test mode");
        const auto root = std::filesystem::absolute(argv[argc - 1]) / AssetId::generate().str();
        std::filesystem::create_directories(root / "Assets");
        transport_admission(root);
        auto bytes = image();
        write(root / "Assets/test.tga", bytes);
        const auto importer = texture_importer(
            direct ? std::filesystem::path{} : std::filesystem::absolute(argv[2]), false);
        AssetImporterRegistry registry;
        registry.add(importer);
        registry.add(texture_importer({}, true));
        registry.seal();
        const ImportTarget target{"portable", "none", "cpu"};
        require(registry.select({"Assets/test.tga", bytes}, target) == importer,
                "Image importer registry selection failed");
        AssetImportRequest request{
            AssetId::generate(), root, "Assets/test.tga", target, {"forge.texture.image"}};
        request.settings = importer->settings().edit(request.settings, "additional_usages",
                                                     nlohmann::json::array({"data"}));
        auto plan = importer->discover(request, {});
        auto prepare = [&](const AssetImportPlan& p) {
            return cook(*importer, request, p, direct);
        };
        auto files = prepare(plan);
        importer->validate({{}, nlohmann::json::object(), files});
        auto corrupt_files = files;
        corrupt_files[0].bytes.back() ^= std::byte{1};
        rejects([&] { importer->validate({{}, nlohmann::json::object(), corrupt_files}); });
        corrupt_files = files;
        corrupt_files.push_back(files[0]);
        rejects([&] { importer->validate({{}, nlohmann::json::object(), corrupt_files}); });
        corrupt_files = files;
        corrupt_files.erase(corrupt_files.begin());
        rejects([&] { importer->validate({{}, nlohmann::json::object(), corrupt_files}); });
        auto texture = decode_texture(files.at(0).bytes);
        require(texture.width == 2 && texture.mips == 2 &&
                    texture.format == TextureFormat::RGBA8Srgb &&
                    texture.subresources[0][0] == std::byte{255},
                "Real image recipe output disagrees");
        require(prepare(plan)[0].bytes == files[0].bytes,
                "Texture recipe is not repeatable within profile");
        ProjectLease lease(root);
        AssetPublisher publisher(lease);
        auto candidate = [&](const AssetImportPlan& p, std::vector<ArtifactFile> output) {
            AssetPublicationCandidate c;
            c.ticket = publisher.capture(request.asset, request.source);
            c.input = p.input;
            c.sidecar.settings = request.settings;
            c.sidecar.identity = {request.asset,
                                  request.source,
                                  p.input.source_digest,
                                  "forge.texture.single.v1",
                                  {}};
            c.sidecar.build_inputs = p.input.document();
            c.records = {{request.asset, "texture", request.source, 1, {}}};
            c.files = std::move(output);
            return c;
        };
        auto result = publisher.publish(candidate(plan, std::move(files)), *importer,
                                        [](const auto&, const auto&) {});
        require(
            AssetCatalog::open_project(root).resolve(AssetRef<TextureAsset>{request.asset}).state ==
                AssetState::Available,
            "Published texture missing from catalog");
        ResourcePool<TextureAsset> selected_pool;
        const auto selected_catalog = std::make_shared<const AssetCatalog>(result.catalog);
        const AssetRef<TextureAsset> selected_ref{request.asset};
        const auto auto_color =
            request_texture(selected_pool, root, selected_catalog, selected_ref);
        require(selected_pool.wait(auto_color, 5s) &&
                    selected_pool.current(selected_ref, "color:auto")->semantic ==
                        TextureSemantic::Color,
                "Catalog-selected color resource failed");
        const auto typed_data = request_texture(selected_pool, root, selected_catalog, selected_ref,
                                                TextureSemantic::Data);
        require(selected_pool.wait(typed_data, 5s) &&
                    selected_pool.current(selected_ref, "data")->format == TextureFormat::RGBA8,
                "Catalog-selected data variant lost its semantics");
        const auto good_color = selected_pool.current(selected_ref, "color:auto");
        auto bad_catalog = result.catalog;
        auto bad_record = bad_catalog.records().at(request.asset);
        bad_record.metadata["forge.import"]["generation"] = 2u;
        bad_record.metadata["forge.import"]["artifact_digest"] = std::string(64, '0');
        bad_catalog.replace(bad_record);
        const auto bad_selection = request_texture(
            selected_pool, root, std::make_shared<const AssetCatalog>(bad_catalog), selected_ref);
        require(!selected_pool.wait(bad_selection, 5s) &&
                    selected_pool.current(selected_ref, "color:auto").identity() ==
                        good_color.identity(),
                "Corrupt catalog selection replaced the usable environment texture");
        const auto missing_variant = request_texture(selected_pool, root, selected_catalog,
                                                     selected_ref, TextureSemantic::Normal);
        require(!selected_pool.wait(missing_variant, 5s) && good_color->width == 2,
                "Missing semantic was silently substituted or damaged a live lease");
        selected_pool.close();
        const auto baseline = read_bytes(root / "forge.assets.json", max_asset_index_bytes);
        DerivedDataCache cache(root);
        auto hit = cache.find(plan.input, [&](const auto& a) { importer->validate(a); });
        require(hit && hit->key == result.artifact.key,
                "Texture cache hit failed format admission");
        const auto cooked = root / ".forge/cache/derived" / result.artifact.key / "texture.json";
        const auto index_bytes = read_bytes(cooked, 16384);
        const auto digest = content_digest(index_bytes);
        const auto index = decode_texture_bundle_index(index_bytes);
        require(index.variants.size() == 2, "Additional usage did not enter atomic bundle");
        ResourcePool<TextureAsset> pool;
        const AssetRef<TextureAsset> ref{request.asset};
        auto ticket = pool.request(
            ref, result.artifact.key, 1,
            texture_bundle_resource_loader(cooked, digest, TextureSemantic::Color), {}, 0, "color");
        require(pool.wait(ticket, 5s) && pool.current(ref, "color")->width == 2,
                "Published texture failed runtime resource load");
        auto data_ticket = pool.request(
            ref, result.artifact.key, 1,
            texture_bundle_resource_loader(cooked, digest, TextureSemantic::Data), {}, 0, "data");
        require(pool.wait(data_ticket, 5s) &&
                    pool.current(ref, "data")->format == TextureFormat::RGBA8 &&
                    pool.current(ref, "color")->format == TextureFormat::RGBA8Srgb &&
                    pool.statistics().selected == 2,
                "Color/data variants replaced each other or changed transfer");
        const auto color_mip =
            std::to_integer<unsigned>(pool.current(ref, "color")->subresources[1][0]);
        const auto data_mip =
            std::to_integer<unsigned>(pool.current(ref, "data")->subresources[1][0]);
        require(color_mip >= 185 && color_mip <= 190 && data_mip >= 127 && data_mip <= 128,
                "Semantic variants shared incorrectly filtered mip pixels");
        auto previous = pool.current(ref, "color");
        // A later incompatible publication never selects the new candidate.
        request.settings = importer->settings().edit(request.settings, "max_size", 1);
        auto smaller = importer->discover(request, {});
        rejects([&] {
            publisher.publish(candidate(smaller, prepare(smaller)), *importer,
                              [](const auto&, const auto&) {
                                  throw std::runtime_error("Live compatibility fixture");
                              });
        });
        require(read_bytes(root / "forge.assets.json", max_asset_index_bytes) == baseline &&
                    previous->width == 2,
                "Rejected publication replaced selected texture");
        // Stale discovery and corrupt source cannot replace the previous selection.
        bytes[20] = std::byte{128};
        write(root / request.source, bytes);
        rejects([&] { importer->import_and_cook(request, smaller, {}, {}); });
        bytes.resize(18);
        write(root / request.source, bytes);
        auto broken = importer->discover(request, {});
        rejects([&] { prepare(broken); });
        require(read_bytes(root / "forge.assets.json", max_asset_index_bytes) == baseline &&
                    previous->width == 2,
                "Malformed reimport changed usable state");
        std::stop_source cancelled;
        cancelled.request_stop();
        rejects([&] { importer->import_and_cook(request, broken, cancelled.get_token(), {}); });
        // Runtime file rejection retains the selected immutable lease as well.
        auto failed = pool.request(
            ref, std::string(64, 'a'), 2,
            texture_bundle_resource_loader(cooked, std::string(64, 'a'), TextureSemantic::Color),
            {}, 0, "color");
        require(!pool.wait(failed, 5s) &&
                    pool.current(ref, "color").identity() == previous.identity(),
                "Failed runtime reload replaced known-good texture");
        pool.close();
        const auto container = texture_importer(
            direct ? std::filesystem::path{} : std::filesystem::absolute(argv[2]), true);
        const auto container_bytes = dds();
        write(root / "Assets/test.dds", container_bytes);
        AssetImportRequest container_request{AssetId::generate(),
                                             root,
                                             "Assets/test.dds",
                                             {"windows", "d3d12", "desktop"},
                                             {"forge.texture.container"}};
        auto container_plan = container->discover(container_request, {});
        const auto container_files = cook(*container, container_request, container_plan, direct);
        container->validate({{}, nlohmann::json::object(), container_files});
        const auto container_texture = decode_texture(container_files[0].bytes);
        require(container_texture.mips == 2 &&
                    container_texture.subresources[1][0] == std::byte{12} &&
                    container_texture.subresources[1][1] == std::byte{34},
                "Container recipe regenerated or lost supplied mip pixels");
        const std::string hdr_header = "#?RADIANCE\nFORMAT=32-bit_rle_rgbe\n\n-Y 1 +X 1\n";
        auto hdr_bytes_view = std::as_bytes(std::span(hdr_header));
        std::vector<std::byte> hdr_bytes(hdr_bytes_view.begin(), hdr_bytes_view.end());
        hdr_bytes.insert(hdr_bytes.end(),
                         {std::byte{128}, std::byte{64}, std::byte{32}, std::byte{130}});
        write(root / "Assets/environment.hdr", hdr_bytes);
        request = {
            AssetId::generate(), root, "Assets/environment.hdr", target, {"forge.texture.image"}};
        const auto hdr_plan = importer->discover(request, {});
        auto hdr_result = publisher.publish(candidate(hdr_plan, prepare(hdr_plan)), *importer,
                                            [](const auto&, const auto&) {});
        ResourcePool<TextureAsset> hdr_pool;
        const auto hdr_catalog = std::make_shared<const AssetCatalog>(hdr_result.catalog);
        const AssetRef<TextureAsset> hdr_ref{request.asset};
        const auto hdr_request = request_texture(hdr_pool, root, hdr_catalog, hdr_ref);
        require(hdr_pool.wait(hdr_request, 5s) &&
                    hdr_pool.current(hdr_ref, "color:auto")->semantic ==
                        TextureSemantic::HdrColor &&
                    hdr_pool.current(hdr_ref, "color:auto")->format == TextureFormat::RGBA32Float,
                "Environment color selection lost HDR semantic/range");
        const auto ldr_request =
            request_texture(hdr_pool, root, hdr_catalog, hdr_ref, TextureSemantic::Color);
        require(!hdr_pool.wait(ldr_request, 5s),
                "Explicit LDR color silently substituted an HDR variant");
        hdr_pool.close();
        if (std::filesystem::exists(root / ".forge/jobs"))
            require(std::filesystem::is_empty(root / ".forge/jobs"),
                    "Finished import staging was not cleaned");
        std::cout << "Texture registry, recipe, cache, publication, resource and failure retention "
                     "passed\n";
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
