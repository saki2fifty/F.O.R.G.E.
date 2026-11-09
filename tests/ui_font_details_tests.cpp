#include "asset_storage.hpp"
#include "editor/ui_font_details.hpp"
#include "imgui_software_capture.hpp"
#include "ui_asset_catalog.hpp"
#include <iostream>

namespace {
void check(bool value, const char* message) {
    if (!value)
        throw std::runtime_error(message);
}
void exercise(const forge::AssetRecord& record, const std::filesystem::path& output, int width) {
    forge::test::ImGuiSoftwareCapture capture(width, 330);
    forge::ui::style(1);
    const auto before = record.metadata;
    const auto frame = [&](const forge::AssetRecord& asset) {
        capture.new_frame();
        ImGui::SetNextWindowPos({0, 0}, ImGuiCond_Always);
        ImGui::SetNextWindowSize({float(width), 330}, ImGuiCond_Always);
        ImGui::Begin("Inspector", nullptr, ImGuiWindowFlags_NoResize);
        ImGui::TextUnformatted("Assets/Fonts/Menu.ttc");
        ImGui::TextUnformatted("Type: ui_font");
        ImGui::LogToBuffer();
        forge::ui::draw_ui_font_details(asset);
        const std::string text = GImGui->LogBuffer.c_str();
        ImGui::LogFinish();
        ImGui::End();
        capture.render();
        return text;
    };
    frame(record);
    const auto text = frame(record);
    check(text.find("Font faces") != text.npos && text.find("Poppins") != text.npos &&
              text.find("Cairo") != text.npos && text.find("Regular") != text.npos &&
              text.find("400") != text.npos,
          "Actual Inspector omitted saved face labels or weight");
    capture.save(output / ("font-faces-" + std::to_string(width) + ".png"));
    check(record.metadata == before, "Read-only font Inspector changed source metadata");
    auto legacy = record;
    legacy.metadata["forge.ui_source"].erase("font_faces");
    check(frame(legacy).find("Register / Refresh") != std::string::npos,
          "Older font metadata did not explain how to discover faces");
    legacy.metadata["forge.ui_source"]["font_faces"] =
        nlohmann::json::array({nullptr, {{"index", -1}}, {{"index", 32}}, {{"index", 0}}});
    check(frame(legacy).find("Unavailable") != std::string::npos,
          "Incomplete optional names lost their safe display");
}
} // namespace
int main(int argc, char** argv) {
    try {
        check(argc == 3, "Need collection example and capture directory");
        const auto output = std::filesystem::absolute(argv[2]);
        const auto root = output / "project";
        std::filesystem::create_directories(root / "Assets/Fonts");
        std::filesystem::copy_file(std::filesystem::path(argv[1]) / "Assets/Fonts/Menu.ttc",
                                   root / "Assets/Fonts/Menu.ttc",
                                   std::filesystem::copy_options::overwrite_existing);
        const auto bytes = forge::asset_storage::read(root / "Assets/Fonts/Menu.ttc");
        forge::ProjectLease lease(root);
        const auto record = forge::register_ui_source(lease, "Assets/Fonts/Menu.ttc");
        const auto catalog = forge::asset_storage::read(root / "forge.assets.json");
        check(forge::register_ui_source(lease, record.source).id == record.id,
              "Refreshing font labels replaced its asset identity");
        for (const auto width : {700, 400})
            exercise(record, output, width);
        check(forge::asset_storage::read(root / "forge.assets.json") == catalog &&
                  forge::asset_storage::read(root / record.source) == bytes,
              "Font discovery or inspection changed saved bytes/identity");
        // Reverse only the two TTC face offsets in the temporary fixture. The
        // font tables themselves remain byte-identical and retain their names.
        auto reordered = bytes.value();
        for (unsigned i = 0; i < 4; ++i)
            std::swap(reordered[12 + i], reordered[16 + i]);
        forge::asset_storage::replace(root / record.source, reordered);
        const auto refreshed = forge::register_ui_source(lease, record.source);
        const auto& faces = refreshed.metadata.at("forge.ui_source").at("font_faces");
        check(refreshed.id == record.id && faces[0].at("family") == "Cairo" &&
                  faces[1].at("family") == "Poppins" &&
                  forge::asset_storage::read(root / record.source) == reordered,
              "Source refresh retained stale face names or changed identity/font bytes");
        forge::asset_storage::replace(root / record.source, bytes.value());
        check(forge::register_ui_source(lease, record.source).metadata == record.metadata,
              "Restoring the source failed to restore derived font metadata");
        std::cout << "Native font Inspector, legacy metadata and stable refresh passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
