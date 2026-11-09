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
void exercise(const forge::AssetRecord& record, const std::filesystem::path& output, int width,
              bool variable = false) {
    const int height = variable ? 440 : 330;
    forge::test::ImGuiSoftwareCapture capture(width, height);
    forge::ui::style(1);
    const auto before = record.metadata;
    const auto frame = [&](const forge::AssetRecord& asset) {
        capture.new_frame();
        ImGui::SetNextWindowPos({0, 0}, ImGuiCond_Always);
        ImGui::SetNextWindowSize({float(width), float(height)}, ImGuiCond_Always);
        ImGui::Begin("Inspector", nullptr, ImGuiWindowFlags_NoResize);
        ImGui::TextUnformatted(record.source.generic_string().c_str());
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
    if (variable)
        check(text.find("wght") != text.npos && text.find("Weight") != text.npos &&
                  text.find("100") != text.npos && text.find("900") != text.npos,
              "Actual Inspector omitted the recorded variable axis");
    else
        check(text.find("Font faces") != text.npos && text.find("Poppins") != text.npos &&
                  text.find("Cairo") != text.npos && text.find("Regular") != text.npos &&
                  text.find("400") != text.npos,
              "Actual Inspector omitted saved face labels or weight");
    capture.save(output /
                 ((variable ? "font-axes-" : "font-faces-") + std::to_string(width) + ".png"));
    check(record.metadata == before, "Read-only font Inspector changed source metadata");
    if (variable) {
        auto malformed = record;
        auto& rows = malformed.metadata["forge.ui_source"]["font_faces"][0]["axes"];
        rows.push_back(nullptr);
        rows.push_back(
            {{"tag", "wght"}, {"minimum", "unavailable"}, {"default", 400}, {"maximum", 900}});
        rows.push_back({{"tag", "wght"}, {"minimum", 900}, {"default", 400}, {"maximum", 100}});
        const auto unchanged = malformed.metadata;
        check(frame(malformed).find("Weight") != std::string::npos &&
                  malformed.metadata == unchanged,
              "Malformed optional axis rows damaged valid read-only Inspector details");
    }
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
        check(argc == 4, "Need collection example, capture directory and original variable font");
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
        std::filesystem::copy_file(argv[3], root / "Assets/Fonts/Weight.ttf",
                                   std::filesystem::copy_options::overwrite_existing);
        const auto variable = forge::register_ui_source(lease, "Assets/Fonts/Weight.ttf");
        const auto variable_bytes = forge::asset_storage::read(root / variable.source);
        for (const auto width : {700, 400})
            exercise(variable, output, width, true);
        check(forge::register_ui_source(lease, variable.source).id == variable.id &&
                  forge::asset_storage::read(root / variable.source) == variable_bytes,
              "Variable axis refresh changed source bytes or identity");
        std::cout << "Native font Inspector, legacy metadata and stable refresh passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
