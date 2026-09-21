#pragma once
namespace forge {
class AssetCatalog;
namespace asset_detail {
struct ModelSelection;
}
} // namespace forge
void test_model_placement(const forge::asset_detail::ModelSelection&, const forge::AssetCatalog&);

void test_model_animation_placement(const forge::asset_detail::ModelSelection&,
                                    const forge::AssetCatalog&);
