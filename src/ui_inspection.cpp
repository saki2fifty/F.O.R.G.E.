#include "ui_inspection.hpp"
#include "asset_bytes.hpp"
#include "asset_storage.hpp"
#include "asset_worker.hpp"
#include "bounded_json.hpp"
#include "ui_inspection_transport.hpp"
namespace forge {
UiAssetSnapshot inspect_ui_dependencies(const std::filesystem::path& executable,
                                        const std::filesystem::path& font,
                                        const std::filesystem::path& project, AssetId id,
                                        std::stop_token stop) {
    const ProjectPaths paths(project);
    if (stop.stop_requested())
        throw std::runtime_error("game.export.cancelled: UI inspection cancelled");
    const auto parent = paths.resolve(".forge/ui-inspection");
    std::filesystem::create_directories(parent);
    const auto stage = parent / AssetId::generate().str();
    if (!std::filesystem::create_directory(stage))
        throw std::runtime_error("export.ui.inspection: Cannot reserve worker staging");
    struct Cleanup {
        std::filesystem::path p;
        ~Cleanup() {
            std::error_code ec;
            if (std::filesystem::weakly_canonical(p, ec) == p && !ec)
                std::filesystem::remove_all(p, ec);
        }
    } cleanup{stage};
    const nlohmann::json request{{"project", path_utf8(paths.root())},
                                 {"font", path_utf8(std::filesystem::absolute(font))},
                                 {"asset", id}};
    asset_storage::replace(stage / "request.json", request.dump());
    auto read = [&](const char* name) {
        return asset_detail::parse_bounded_json(asset_detail::read_bytes(stage / name, 1024 * 1024),
                                                1024 * 1024, 100000, 16);
    };
    try {
        asset_detail::run_worker(asset_detail::WorkerKind::UiInspection, executable, stage, stop);
    } catch (...) {
        if (!stop.stop_requested() && std::filesystem::is_regular_file(stage / "error.json"))
            throw std::runtime_error(read("error.json").at("message").get<std::string>());
        throw;
    }
    auto result = ui_inspection_detail::decode(read("result.json"));
    if (result.project != paths.root() || !result.documents.contains(id))
        throw std::runtime_error("export.ui.inspection: Worker returned a different document");
    return result; // Caller re-admits exact bytes before using this in the catalog.
}
} // namespace forge
