#include "audio_importer.hpp"
#include "model_importer.hpp"
#include "sprite_atlas_source.hpp"
#include "texture_image_process.hpp"
#include "texture_importer.hpp"
#include "tile_asset_worker.hpp"
#include "webp_sequence.hpp"
#include <chrono>
#include <thread>
int main(int argc, char** argv) {
    if (argc != 2 || std::string_view(argv[1]) != "--build-asset")
        return 2;
    const auto staging = std::filesystem::current_path();
    try {
        std::stop_source cancellation;
        std::jthread monitor([&](std::stop_token done) {
            while (!done.stop_requested()) {
                std::error_code error;
                const bool cancelled = std::filesystem::exists(staging / "cancel.request", error);
                if (cancelled || error) {
                    cancellation.request_stop();
                    return;
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            }
        });
        std::string profile;
        const auto recipe = forge::asset_detail::read_import_process_recipe(staging, &profile);
        const bool model = recipe == "forge.model.gltf";
        const bool atlas = recipe == forge::asset_detail::sprite_atlas_source_recipe;
        const bool atlas_recipe = recipe == forge::asset_detail::sprite_atlas_importer_id;
        const bool audio = forge::asset_detail::audio_source_profile(recipe) != nullptr;
        const bool tile =
            recipe == forge::tile_set_importer_id || recipe == forge::tile_map_importer_id;
        const bool image_decode = recipe == forge::asset_detail::texture_image_decode_recipe;
        const bool webp_sequence = recipe == forge::asset_detail::webp_sequence_recipe;
        if (!model && !audio && !tile && !atlas && !atlas_recipe && !image_decode &&
            !webp_sequence && recipe != "forge.texture.image" &&
            recipe != "forge.texture.container")
            throw std::runtime_error("Unsupported import worker recipe");
        const auto limits =
            webp_sequence ? forge::asset_detail::webp_sequence_worker_limits()
            : image_decode
                ? forge::asset_detail::texture_image_worker_limits(
                      profile == forge::asset_detail::texture_signed_image_decode_profile ||
                      profile == forge::asset_detail::texture_hdr_image_decode_profile)
            : atlas || atlas_recipe ? forge::asset_detail::sprite_atlas_source_limits()
            : model                 ? forge::asset_detail::model_worker_limits()
            : audio                 ? forge::asset_detail::audio_worker_limits()
            : tile                  ? forge::asset_detail::tile_worker_limits()
                                    : forge::asset_detail::texture_worker_limits();
        auto request = forge::asset_detail::read_import_process_request(staging, limits);
        if (request.payload.at("recipe") != recipe)
            throw std::runtime_error("Import recipe changed while reading snapshot");
        if (image_decode && request.payload.at("profile") != profile)
            throw std::runtime_error("Image profile changed while reading snapshot");
        auto output =
            webp_sequence  ? forge::asset_detail::execute_webp_sequence_recipe(
                                 std::move(request), cancellation.get_token())
            : image_decode ? forge::asset_detail::execute_texture_image_recipe(
                                 request, cancellation.get_token())
            : atlas_recipe ? forge::asset_detail::execute_sprite_atlas_recipe(
                                 request, cancellation.get_token())
            : atlas ? forge::asset_detail::execute_sprite_atlas_source(request,
                                                                       cancellation.get_token())
            : model ? forge::asset_detail::execute_model_recipe(std::move(request),
                                                                cancellation.get_token())
            : audio ? forge::asset_detail::execute_audio_recipe(request, cancellation.get_token())
            : tile ? forge::asset_detail::execute_tile_recipe(request, cancellation.get_token())
                   : forge::asset_detail::execute_texture_recipe(request, cancellation.get_token());
        if (cancellation.stop_requested())
            throw std::runtime_error("Import cancelled before result publication");
        forge::asset_detail::write_import_process_result(staging, output, limits);
        return 0;
    } catch (const std::exception& e) {
        forge::asset_detail::write_import_process_error(staging, "asset.import.failed", e.what());
        return 1;
    }
}
