#pragma once
#include "asset_storage.hpp"
#include "runtime_dependencies.hpp"
#include "runtime_package.hpp"
#include "runtime_resource_fixtures.hpp"
#include <cstdlib>
#include <forge/native_sdk.hpp>
#include <forge/runtime_resources.hpp>
#include <forge/scene.hpp>
#include <forge/sdk_client.hpp>
#include <thread>
inline void test_sdk_package_resources(const std::filesystem::path& library,
                                       const std::filesystem::path& scratch) {
    using namespace forge;
    using namespace std::chrono_literals;
    auto require = [](bool ok, const char* why) {
        if (!ok)
            throw std::runtime_error(why);
    };
    const auto base = scratch / ("sdk-package-" + AssetId::generate().str());
    const auto project = base / "source", package = base / "relocated";
    std::filesystem::create_directories(project);
    struct Cleanup {
        std::filesystem::path path;
        ~Cleanup() {
            std::error_code ec;
            std::filesystem::remove_all(path, ec);
        }
    } cleanup{base};
    AssetCatalog catalog(project);
    Json scene_document;
    {
        WorldContext w;
        Scene scene(w);
        scene_document = scene.snapshot();
    }
    const auto scene_id = scene_document.at("asset_id").get<AssetId>();
    asset_storage::replace(project / "main.scene.json", scene_document.dump());
    catalog.add(
        {scene_id, "scene", "main.scene.json", scene_document.at("version").get<unsigned>()});
    const auto selected = test::runtime_texture_fixture(project, catalog);
    auto texture = catalog.records().at(selected);
    auto& recipe = texture.metadata["forge.import"];
    recipe["platform"] = "linux";
    recipe["backend"] = "none";
    recipe["profile"] = "cpu";
    asset_storage::replace(project / texture.source, "fixture source");
    catalog.replace(texture);
    auto other = texture;
    other.id = AssetId::generate();
    other.source = "undeclared.texture";
    asset_storage::replace(project / other.source, "undeclared fixture source");
    catalog.add(other);
    catalog.save(project / "forge.assets.json");
    {
        ProjectLease writer(project);
        declare_runtime_dependencies(writer, scene_id,
                                     {{selected,
                                       "texture",
                                       AssetDependencyKind::Runtime,
                                       "declared:module-selected skin",
                                       {}}},
                                     catalog.document());
    }
    const std::array roots{scene_id};
    package_runtime_content(project, package, roots, {"linux", "none"});
    std::filesystem::rename(project, base / "unavailable");
    struct Environment {
        static void set(const char* name, const std::string& value) {
#ifdef _WIN32
            _putenv_s(name, value.c_str());
#else
            if (value.empty())
                unsetenv(name);
            else
                setenv(name, value.c_str(), 1);
#endif
        }
        ~Environment() {
            set("FORGE_SDK_PACKAGE_RESOURCE", "");
            set("FORGE_SDK_PACKAGE_UNDECLARED", "");
        }
    } environment;
    Environment::set("FORGE_SDK_PACKAGE_RESOURCE", selected.str());
    Environment::set("FORGE_SDK_PACKAGE_UNDECLARED", other.id.str());
    auto module = load_native_sdk(library, "project.sdk_probe", "1");
    EngineContext engine(WorldRole::Runtime, false, {runtime_resources_module(package), module});
    auto& w = engine.world().world();
    const auto token = w.lookup("sdk.dynamic_resource").get<uint64_t>();
    const auto* host = *static_cast<const ForgeSdkWorldV1* const*>(
        ecs_get_id(w.c_ptr(), w.lookup("sdk.host").id(), w.lookup("sdk.HostProbe").id()));
    sdk::Client client(host);
    const auto deadline = std::chrono::steady_clock::now() + 5s;
    for (;;) {
        engine.services().resources()->synchronize();
        ForgeSdkResourceV1 status{};
        require(client.inspect_resource(token, status), "Native module lost package subscription");
        if (std::string_view(status.state) == "ready")
            break;
        require(std::string_view(status.state) != "failed" &&
                    std::chrono::steady_clock::now() < deadline,
                "Native module could not load declared texture after source relocation");
        std::this_thread::sleep_for(1ms);
    }
    require(client.release_resource(token), "Native module could not release packaged resource");
}
