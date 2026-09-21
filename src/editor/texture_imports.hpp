#pragma once
#include "../texture_authoring.hpp"
#include "asset_import_editor.hpp"
namespace forge {
class TextureImportEditor : public AssetImportEditor {
  public:
    explicit TextureImportEditor(std::filesystem::path worker)
        : AssetImportEditor(
              {"texture_import", "Texture import", "Assets/texture.png",
               "PNG, JPEG, TGA, BMP, WebP, HDR/RGBE, DDS, KTX or KTX2 inside this project. Cooking "
               "runs in a separate worker.",
               "One texture AssetId can provide independent color, data and normal variants.",
               "Import texture...", desktop_texture_target(),
               [worker = std::move(worker)] { return texture_import_registry(worker); },
               [](auto& candidate, const auto& plan, const auto&, auto) {
                   prepare_texture_publication(candidate, plan);
               }}) {}
};
} // namespace forge
