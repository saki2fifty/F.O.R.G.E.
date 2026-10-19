#pragma once
#include "sprite_frame_picker.hpp"
#include "sprite_sheet_preview.hpp"

namespace forge::ui {
// Borrow the one transient preview; its host retires it after UI submission.
inline SpriteSheetImage sprite_sheet_image(SpriteSheetPreview& preview,
                                           std::shared_ptr<const AssetCatalog> catalog,
                                           AssetRef<MaterialAsset> material) {
    preview.select(std::move(catalog), material);
    preview.pump();
    SpriteSheetImage image;
    image.status = preview.error();
    if (const auto* data = preview.data()) {
        image.width = data->width;
        image.height = data->height;
        image.revision = preview.identity();
        image.render = [&preview](unsigned width, unsigned height, std::array<float, 4> region) {
            return reinterpret_cast<ImTextureID>(preview.render(width, height, region));
        };
    }
    if (!image.status.empty())
        image.retry = [&preview] { preview.retry(); };
    return image;
}
} // namespace forge::ui
