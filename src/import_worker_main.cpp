#include "audio_importer.hpp"
#include "model_importer.hpp"
#include "texture_importer.hpp"
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
        const auto recipe = forge::asset_detail::read_import_process_recipe(staging);
        const bool model = recipe == "forge.model.gltf";
        const bool audio = recipe == "forge.audio.wav";
        if (!model && !audio && recipe != "forge.texture.image" &&
            recipe != "forge.texture.container")
            throw std::runtime_error("Unsupported import worker recipe");
        const auto limits = model   ? forge::asset_detail::model_worker_limits()
                            : audio ? forge::asset_detail::audio_worker_limits()
                                    : forge::asset_detail::texture_worker_limits();
        auto request = forge::asset_detail::read_import_process_request(staging, limits);
        if (request.payload.at("recipe") != recipe)
            throw std::runtime_error("Import recipe changed while reading snapshot");
        auto output =
            model ? forge::asset_detail::execute_model_recipe(std::move(request),
                                                              cancellation.get_token())
            : audio
                ? forge::asset_detail::execute_audio_recipe(request, cancellation.get_token())
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
