#pragma once
#include "standalone_manifest.hpp"
#include <forge/runtime_execution.hpp>
namespace test_worker_package {
template <class Check, class Reject>
void exercise(const std::filesystem::path& root, Check check, Reject reject) {
    using namespace forge;
    using Json = nlohmann::json;
    const AssetCatalog content(root / "worker-kit");
    const Json serial{{"ecs_workers", 1}}, parallel{{"ecs_workers", 4}}, legacy = Json::object();
    validate_runtime_compatibility(content, Json::object(), &serial);
    validate_runtime_compatibility(content, Json::object(), &legacy);
    validate_runtime_compatibility(
        content, {{"runtime_ecs_workers", runtime_ecs_worker_profile_version}}, &parallel);
    reject([&] { validate_runtime_compatibility(content, Json::object(), &parallel); },
           "Parallel project accepted a kit without fixed ECS worker support");
    for (const Json& invalid : {Json(), Json(false), Json(0), Json(1.), Json("1"), Json(2)})
        reject([&] {
            validate_runtime_compatibility(content, {{"runtime_ecs_workers", invalid}}, &parallel);
        });
    check(content.records().empty(), "Worker kit admission created a parallel asset authority");
}
} // namespace test_worker_package
