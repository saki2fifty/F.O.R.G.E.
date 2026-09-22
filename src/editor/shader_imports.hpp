#pragma once
#include "../asset_bytes.hpp"
#include "../bounded_json.hpp"
#include "../shader_authoring.hpp"
#include "../shader_pipeline.hpp"
#include "asset_import_editor.hpp"
namespace forge {
class ShaderImportEditor : public AssetImportEditor {
    std::uint64_t settings_generation_ = 0;
    std::map<std::string, std::vector<std::string>> axes_;
    std::string settings_error_;

  public:
    ShaderImportEditor(std::filesystem::path worker,
                       std::function<asset_detail::ShaderCompilerProfile()> compiler)
        : AssetImportEditor(
              {"shader_import",
               "Shader import",
               "Assets/Surface.shader.json",
               "A .shader.json program inside this project. HLSL/includes compile in "
               "a separate worker.",
               "Build a shader asset for Windows D3D12. Its declared stages and "
               "permutation are validated before replacing the published revision.",
               "Import shader...",
               {"windows-x64", "d3d12", "fxc-5.1"},
               [worker = std::move(worker), compiler = std::move(compiler)] {
                   auto registry = std::make_shared<AssetImporterRegistry>();
                   registry->add(asset_detail::shader_importer(worker, compiler()));
                   registry->seal();
                   return registry;
               },
               [](auto& candidate, const auto& plan, const auto&, auto) {
                   prepare_shader_publication(candidate, plan);
               },
               [](const auto& project, const auto& source) {
                   const auto bytes =
                       asset_detail::read_bytes(ProjectPaths(project).resolve(source), 1024 * 1024);
                   const auto document = asset_detail::parse_bounded_json(bytes, 1024 * 1024);
                   (void)shader_program_source(document);
                   return document.at("asset_id").template get<AssetId>();
               },
               [](auto& problem, const auto& project, const auto& source) {
                   const auto bytes =
                       asset_detail::read_bytes(ProjectPaths(project).resolve(source), 1024 * 1024);
                   const auto document = asset_detail::parse_bounded_json(bytes, 1024 * 1024);
                   const auto root = ProjectPaths::normalize(std::filesystem::u8path(
                       document.at("source_root").template get<std::string>()));
                   ui::shader_diagnostic_location(problem, project, root);
               }}) {
        draw_settings = [this](AssetImportDraft& draft, std::string& error) {
            if (settings_generation_ != selection_generation()) {
                settings_generation_ = selection_generation();
                axes_.clear();
                settings_error_.clear();
                try {
                    const auto bytes = asset_detail::read_bytes(
                        ProjectPaths(draft.request.project).resolve(draft.request.source),
                        1024 * 1024);
                    axes_ =
                        shader_program_source(asset_detail::parse_bounded_json(bytes, 1024 * 1024))
                            .permutations;
                } catch (const std::exception& e) {
                    settings_error_ = e.what();
                }
            }
            if (!settings_error_.empty()) {
                ui::field_error(settings_error_);
                ImGui::TextWrapped("Review the source again after fixing its shader declaration.");
                return;
            }
            ui::import_settings_fields(draft.importer->settings(), draft.request.settings, error,
                                       &axes_);
        };
    }
};
} // namespace forge
