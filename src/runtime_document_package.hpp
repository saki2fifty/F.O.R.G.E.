#pragma once
#include "runtime_package.hpp"
namespace forge::package_detail {
bool authored_document(const AssetRecord&);
nlohmann::json admit_document(const AssetRecord&, std::span<const std::byte>);
// Populate the SAME catalog-owned typed graph from authoritative Meta and the
// current reachable documents. Never edits the source project catalog.
void prepare_documents(AssetCatalog&, const std::filesystem::path&, std::span<const AssetId>,
                       const nlohmann::json& schema, RuntimePackageLimits, std::stop_token);
} // namespace forge::package_detail
