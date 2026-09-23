#pragma once
#include "material_authoring.hpp"
#include "material_selection.hpp"
#include "runtime_dependencies.hpp"
#include "runtime_package.hpp"
#include "runtime_resource_fixtures.hpp"
#include <forge/engine_assets.hpp>
#include <forge/material_source.hpp>
#include <forge/runtime_resources.hpp>
#include <forge/scene.hpp>
#include <fstream>
#include <future>
namespace forge::test {
inline void runtime_resource_services(const std::filesystem::path& parent) {
    using namespace std::chrono_literals;
    auto check = [](bool ok, const char* why) {
        if (!ok)
            throw std::runtime_error(why);
    };
    auto reject = [&](auto fn) {
        bool failed = false;
        try {
            fn();
        } catch (const std::exception&) {
            failed = true;
        }
        check(failed, "Invalid runtime resource operation accepted");
    };
    const auto root = parent / AssetId::generate().str();
    std::filesystem::create_directories(root);
    auto lease = std::make_shared<ProjectLease>(root);
    auto document = MaterialSource::create(AssetId::generate());
    const auto asset = document.asset();
    AssetCatalog initial(root);
    initial.add({asset, "material", "surface.material.json"});
    initial.save(AssetCatalog::project_index(root));
    auto importer = std::make_unique<AssetImportService>(lease, material_import_registry(),
                                                         ImportTarget{"linux", "none", "cpu"});
    auto publish = [&] {
        {
            std::ofstream out(root / "surface.material.json");
            out << document.document.dump();
            check(bool(out.flush()), "Failed material fixture save");
        }
        importer->submit(
            importer->prepare("surface.material.json"),
            [](auto& candidate, const auto& plan, const auto&) {
                prepare_material_publication(candidate, plan);
            },
            [](const auto&, const auto& artifact) {
                (void)asset_detail::decode_material_bundle(artifact.files);
            });
        check(importer->wait_idle(10s), "Fixture material import stalled");
        auto done = importer->poll();
        check(done.size() == 1 && done.front().published, "Fixture material import failed");
    };
    publish();
    auto published = AssetCatalog::open_project(root);
    const auto texture_asset = runtime_texture_fixture(root, published);
    const auto shader_asset = runtime_shader_fixture(root, published);
    published.save(AssetCatalog::project_index(root));
    const auto saved = published.document();
    std::shared_ptr<RuntimeResourceService> expired;
    ServiceAccess stale;
    {
        EngineContext engine(WorldRole::Runtime, false, {runtime_resources_module(root)});
        auto service = engine.services().resources();
        expired = service;
        stale = engine.services();
        check(engine.services().available(Capability::Resources) &&
                  !engine.services().available(Capability::Rendering),
              "CPU resource capability requires a GPU");
        Scene scene(engine.world());
        const auto before = scene.snapshot();
        auto wait = [&](std::uint64_t token, auto predicate) {
            const auto end = std::chrono::steady_clock::now() + 10s;
            for (;;) {
                service->synchronize();
                auto status = service->inspect(token);
                if (predicate(status))
                    return status;
                if (std::chrono::steady_clock::now() >= end)
                    throw std::runtime_error("Runtime resource did not reach expected state: " +
                                             status.state + " " + status.diagnostic);
                std::this_thread::sleep_for(1ms);
            }
        };
        const auto mesh = service->request(RuntimeResourceKind::Mesh, engine_primitive(0).id);
        const auto material = service->request(RuntimeResourceKind::Material, asset);
        const auto material2 = service->request(RuntimeResourceKind::Material, asset);
        auto ready = [](const auto& s) {
            return s.state == "ready" && !s.retained_revision.empty();
        };
        wait(mesh, ready);
        const auto first = wait(material, ready);
        const auto shader = service->request(RuntimeResourceKind::Shader, shader_asset);
        wait(shader, ready);
        std::vector<std::uint64_t> texture_tokens;
        for (auto variant : {RuntimeTextureVariant::Automatic, RuntimeTextureVariant::Color,
                             RuntimeTextureVariant::Data, RuntimeTextureVariant::Normal,
                             RuntimeTextureVariant::HdrColor}) {
            const auto token =
                service->request(RuntimeResourceKind::Texture, texture_asset, variant);
            wait(token, ready);
            texture_tokens.push_back(token);
        }
        reject([&] {
            service->request(RuntimeResourceKind::Texture, texture_asset,
                             static_cast<RuntimeTextureVariant>(99));
        });
        reject([&] {
            service->request(RuntimeResourceKind::Mesh, engine_primitive(0).id,
                             RuntimeTextureVariant::Data);
        });
        check(wait(material2, ready).retained_revision == first.retained_revision,
              "Coalesced subscriptions disagree");
        check(service->release(material2) && !service->release(material2), "Release is not scoped");
        reject([&] { service->inspect(material2); });
        reject([&] { service->request(RuntimeResourceKind::Shader, asset); });
        reject([&] { service->request(static_cast<RuntimeResourceKind>(99), asset); });
        reject([&] { service->request(RuntimeResourceKind::Mesh, {}); });
        check(std::async(std::launch::async,
                         [&] {
                             try {
                                 service->inspect(mesh);
                             } catch (...) {
                                 return true;
                             }
                             return false;
                         })
                  .get(),
              "Off-thread runtime resource access accepted");
        {
            EngineContext other(WorldRole::Runtime, false, {runtime_resources_module(root)});
            reject([&] { other.services().resources()->inspect(mesh); });
        }
        // A catalog with a newer but nonexistent cooked selection must keep old bytes.
        auto catalog = AssetCatalog::open_project(root);
        auto record = catalog.records().at(asset);
        record.metadata["forge.import"]["key"] = std::string(64, 'f');
        record.metadata["forge.import"]["generation"] = std::uint64_t(2);
        catalog.replace(record);
        for (auto id : {texture_asset, shader_asset}) {
            auto invalid = catalog.records().at(id);
            invalid.metadata["forge.import"]["key"] = std::string(64, 'f');
            invalid.metadata["forge.import"]["generation"] = std::uint64_t(2);
            catalog.replace(std::move(invalid));
        }
        catalog.save(AssetCatalog::project_index(root));
        service->refresh();
        const auto failed = wait(material, [](const auto& s) { return s.state == "failed"; });
        check(failed.retained_revision == first.retained_revision && !failed.diagnostic.empty(),
              "Failed replacement dropped last-good resource");
        for (auto token : texture_tokens) {
            const auto status = wait(token, [](const auto& s) { return s.state == "failed"; });
            check(!status.retained_revision.empty() && !status.diagnostic.empty(),
                  "Failed texture variant replacement lost the retained revision");
        }
        const auto failed_shader = wait(shader, [](const auto& s) { return s.state == "failed"; });
        check(!failed_shader.retained_revision.empty() && !failed_shader.diagnostic.empty(),
              "Failed shader replacement lost the retained revision");
        catalog.restore(saved);
        // Monotonic source generations: a reverted file is a new publication.
        for (auto id : {asset, texture_asset, shader_asset}) {
            auto restored = catalog.records().at(id);
            restored.metadata["forge.import"]["generation"] = std::uint64_t(3);
            catalog.replace(std::move(restored));
        }
        catalog.save(AssetCatalog::project_index(root));
        service->refresh();
        wait(material, ready);
        for (auto token : texture_tokens) {
            wait(token, ready);
            service->release(token);
        }
        wait(shader, ready);
        service->release(shader);
        // Publish a real new revision. Existing subscription observes it without re-request.
        document.document["overrides"]["parameters"]["roughnessFactor"] = {
            {"type", unsigned(MaterialParameterType::Scalar)}, {"value", {0.17}}};
        publish();
        service->refresh();
        const auto second = wait(material, [&](const auto& s) {
            return ready(s) && s.retained_revision != first.retained_revision;
        });
        check(second.requested_revision == second.retained_revision,
              "Ready replacement did not pin its selected revision");
        std::vector<std::uint64_t> bounded;
        for (unsigned i = 0; i < 254; ++i)
            bounded.push_back(service->request(RuntimeResourceKind::Mesh, engine_primitive(0).id));
        reject([&] { service->request(RuntimeResourceKind::Mesh, engine_primitive(0).id); });
        for (auto token : bounded)
            service->release(token);
        check(scene.snapshot() == before && !scene.can_undo(),
              "Resource service changed scene or authoring history");
        // Pending refresh must drain safely at world shutdown, including retained clients.
        service->refresh();
    }
    check(!stale.available(Capability::Resources), "Dead world still advertises resources");
    reject([&] { expired->synchronize(); });
    reject([&] { expired->request(RuntimeResourceKind::Mesh, engine_primitive(0).id); });
    expired.reset();
    {
        EngineContext authoring(WorldRole::Authoring, false, {runtime_resources_module(root)});
        check(!authoring.services().available(Capability::Resources),
              "Runtime provider activated in authored world");
    }
    // A module-selected resource must be in the admitted finite package closure.
    auto current = AssetCatalog::open_project(root);
    auto texture_record = current.records().at(texture_asset);
    texture_record.metadata["forge.import"]["platform"] = "linux";
    texture_record.metadata["forge.import"]["backend"] = "none";
    texture_record.metadata["forge.import"]["profile"] = "cpu";
    current.replace(texture_record);
    current.save(AssetCatalog::project_index(root));
    {
        std::ofstream source(root / texture_record.source);
        source << "fixture authoring source";
    }
    declare_runtime_dependencies(*lease, asset,
                                 {{texture_asset,
                                   "texture",
                                   AssetDependencyKind::Runtime,
                                   "declared:gameplay skin selection",
                                   {}}},
                                 current.document());
    publish();
    const auto recooked = AssetCatalog::open_project(root);
    check(std::any_of(recooked.records().at(asset).dependency_edges.begin(),
                      recooked.records().at(asset).dependency_edges.end(),
                      is_declared_runtime_dependency),
          "Reimport discarded runtime declaration");
    const auto package = parent / ("module-package-" + AssetId::generate().str());
    const std::array roots{asset};
    package_runtime_content(root, package, roots, {"linux", "none"});
    importer.reset(); // Joins the worker and releases its shared project writer lease.
    lease.reset();
    const auto offline = parent / ("module-source-offline-" + AssetId::generate().str());
    std::filesystem::rename(root, offline);
    {
        EngineModule consumer;
        consumer.id = "test.dynamic_consumer";
        consumer.dependencies = {"forge.resources"};
        consumer.runtime_roles = role_mask(WorldRole::Runtime);
        consumer.required_services = capability(Capability::Resources);
        consumer.allowed_services = capability(Capability::Resources);
        std::uint64_t token = 0;
        consumer.start = [&](ModuleContext& c) {
            token = c.services.resources()->request(RuntimeResourceKind::Texture, texture_asset);
            try {
                (void)c.services.resources()->request(RuntimeResourceKind::Shader, shader_asset);
                throw std::runtime_error("Undeclared dynamic shader accepted");
            } catch (const std::exception& e) {
                check(std::string_view(e.what()).starts_with("package.resource.undeclared:"),
                      "Module request lacks structured undeclared-resource diagnostic");
            }
        };
        EngineContext engine(WorldRole::Runtime, false,
                             {runtime_resources_module(package), consumer});
        const auto service = engine.services().resources();
        const auto end = std::chrono::steady_clock::now() + 5s;
        for (;;) {
            service->synchronize();
            const auto status = service->inspect(token);
            if (status.state == "ready")
                break;
            check(status.state != "failed" && std::chrono::steady_clock::now() < end,
                  "Declared module request did not become ready after relocation");
            std::this_thread::sleep_for(1ms);
        }
        service->refresh(); // Immutable package does not discover the offline source catalog.
        check(service->inspect(token).state == "ready",
              "Rejected request destabilized the active resource");
    }
    std::filesystem::rename(offline, root);
}
} // namespace forge::test
