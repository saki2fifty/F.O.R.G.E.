#include "asset_bytes.hpp"
#include "asset_storage.hpp"
#include "sequence_encoding.hpp"
#include "view_image.hpp"
#include <forge/identity.hpp>
#include <iostream>

using namespace forge;
using namespace forge::asset_detail;
using Json = nlohmann::json;
namespace {
void check(bool value, const char* reason) {
    if (!value)
        throw std::runtime_error(reason);
}
template <class F> void rejects(F action, std::string_view message = {}) {
    try {
        action();
    } catch (const std::exception& error) {
        if (!message.empty() &&
            std::string_view(error.what()).find(message) == std::string_view::npos)
            throw std::runtime_error("Unexpected movie rejection: " + std::string(error.what()));
        return;
    }
    throw std::runtime_error("Invalid AVI sequence was accepted");
}
Rgba8Pixels picture(unsigned frame) {
    Rgba8Pixels value{7, 5, std::vector<std::uint8_t>(7 * 5 * 4), true};
    for (unsigned i = 0; i < 35; ++i) {
        value.rgba[i * 4] = std::uint8_t(30 + 70 * frame);
        value.rgba[i * 4 + 1] = std::uint8_t(i * 4);
        value.rgba[i * 4 + 2] = std::uint8_t(220 - 60 * frame);
        value.rgba[i * 4 + 3] = 255;
    }
    return value;
}
ImportProcessRequest request(const std::filesystem::path& folder, bool sound = false) {
    const auto rate = avi_movie_frame_rate(30000. / 1001);
    ImportProcessRequest value{{{"recipe", sound ? avi_game_recipe : avi_sequence_recipe},
                                {"profile", sound ? avi_game_profile : avi_sequence_profile},
                                {"width", 7u},
                                {"height", 5u},
                                {"rate", rate.rate},
                                {"scale", rate.scale},
                                {"frames", 3u},
                                {"jpeg_quality", 90}},
                               {}};
    for (unsigned i = 0; i < 3; ++i)
        value.inputs.push_back(
            {"frame-" + std::to_string(i) + ".png",
             read_bytes(folder / (std::to_string(i) + ".png"), FORGE_VIEW_IMAGE_ENCODED_LIMIT)});
    if (sound)
        value.inputs.push_back(
            {"audio.s16le", read_bytes(folder / "audio.s16le", avi_game_audio_byte_limit)});
    return value;
}
void game_direct(const std::filesystem::path& folder) {
    std::vector<std::byte> pcm;
    for (unsigned frame = 0; frame < 4805; ++frame)
        for (const auto sample :
             {std::uint16_t(frame % 2 ? 0x8000 : 0x7fff), std::uint16_t(frame * 3)}) {
            pcm.push_back(std::byte(sample & 255));
            pcm.push_back(std::byte(sample >> 8));
        }
    asset_storage::replace(folder / "audio.s16le",
                           std::string_view(reinterpret_cast<const char*>(pcm.data()), pcm.size()));
    auto files = execute_avi_sequence_recipe(request(folder, true));
    check(files.size() == 1 && files[0].name == "game.avi",
          "Game worker returned the wrong profile");
    asset_storage::replace(folder / "direct-game.avi",
                           std::string_view(reinterpret_cast<const char*>(files[0].bytes.data()),
                                            files[0].bytes.size()));
    auto bad = request(folder, true);
    bad.inputs.back().bytes.pop_back();
    rejects([&] { (void)execute_avi_sequence_recipe(std::move(bad)); }, "exact video duration");
    bad = request(folder, true);
    bad.inputs.back().name = "../audio.s16le";
    rejects([&] { (void)execute_avi_sequence_recipe(std::move(bad)); }, "exact video duration");
    bad = request(folder, true);
    bad.inputs.pop_back();
    rejects([&] { (void)execute_avi_sequence_recipe(std::move(bad)); }, "frame count");
    bad = request(folder, true);
    bad.payload["profile"] = avi_sequence_profile;
    rejects([&] { (void)execute_avi_sequence_recipe(std::move(bad)); }, "profile");
    bad = request(folder, true);
    bad.payload["frames"] = 4096u;
    bad.payload["rate"] = 1u;
    bad.payload["scale"] = 1u;
    bad.inputs.resize(4097);
    rejects([&] { (void)execute_avi_sequence_recipe(std::move(bad)); }, "at most128MiB");
    bad = request(folder, true);
    bad.inputs[1].bytes = read_bytes(folder / "transparent.png", FORGE_VIEW_IMAGE_ENCODED_LIMIT);
    rejects([&] { (void)execute_avi_sequence_recipe(std::move(bad)); }, "opaque alpha");
    std::stop_source stopped;
    stopped.request_stop();
    rejects([&] { (void)execute_avi_sequence_recipe(request(folder, true), stopped.get_token()); },
            "cancelled");
}
void direct(const std::filesystem::path& folder) {
    const auto complete = execute_avi_sequence_recipe(request(folder));
    check(complete.size() == 1 && complete[0].name == "animation.avi" &&
              complete[0].bytes.size() > 224,
          "Worker did not return its completed movie");
    for (const auto* field : {"width", "height", "rate", "scale", "frames", "jpeg_quality"}) {
        for (const auto& bad : {Json(-1), Json(true), Json(1.25), Json(0)}) {
            auto invalid = request(folder);
            invalid.payload[field] = bad;
            rejects([&] { (void)execute_avi_sequence_recipe(std::move(invalid)); });
        }
    }
    auto invalid = request(folder);
    invalid.payload["unknown"] = true;
    rejects([&] { (void)execute_avi_sequence_recipe(std::move(invalid)); }, "profile");
    invalid = request(folder);
    invalid.payload["profile"] = "other";
    rejects([&] { (void)execute_avi_sequence_recipe(std::move(invalid)); }, "profile");
    invalid = request(folder);
    invalid.payload["width"] = 4097u;
    rejects([&] { (void)execute_avi_sequence_recipe(std::move(invalid)); });
    invalid = request(folder);
    invalid.payload["rate"] = 60000u;
    invalid.payload["scale"] = 2002u;
    rejects([&] { (void)execute_avi_sequence_recipe(std::move(invalid)); }, "reduced rational");
    invalid = request(folder);
    invalid.inputs.pop_back();
    rejects([&] { (void)execute_avi_sequence_recipe(std::move(invalid)); }, "frame count");
    invalid = request(folder);
    invalid.inputs[1].name = "../outside.png";
    rejects([&] { (void)execute_avi_sequence_recipe(std::move(invalid)); }, "snapshot");
    invalid = request(folder);
    invalid.inputs[1].bytes.resize(8);
    rejects([&] { (void)execute_avi_sequence_recipe(std::move(invalid)); });
    const auto transparent = folder / "transparent.png";
    auto alpha = picture(1);
    alpha.rgba[3] = 100;
    save_image_pixels(transparent, alpha);
    invalid = request(folder);
    invalid.inputs[1].bytes = read_bytes(transparent, FORGE_VIEW_IMAGE_ENCODED_LIMIT);
    rejects([&] { (void)execute_avi_sequence_recipe(std::move(invalid)); }, "opaque alpha");
    auto unlabelled = picture(1);
    unlabelled.srgb = false;
    save_image_pixels(folder / "unlabelled.png", unlabelled);
    invalid = request(folder);
    invalid.inputs[1].bytes = read_bytes(folder / "unlabelled.png", FORGE_VIEW_IMAGE_ENCODED_LIMIT);
    rejects([&] { (void)execute_avi_sequence_recipe(std::move(invalid)); }, "sRGB RGBA8");
    std::stop_source stopped;
    stopped.request_stop();
    rejects([&] { (void)execute_avi_sequence_recipe(request(folder), stopped.get_token()); },
            "cancelled");
}
void process(const std::filesystem::path& worker, const std::filesystem::path& root,
             const std::filesystem::path& folder) {
    const std::vector<std::string> names{"0.png", "1.png", "2.png"};
    const auto info = write_avi_sequence(worker, root, folder, names, 7, 5, 30000. / 1001);
    const auto game = write_avi_game(worker, root, folder, names, 7, 5, 30000. / 1001);
    const auto game_bytes = read_bytes(folder / "game.avi", avi_movie_byte_limit);
    check(game.bytes == game_bytes.size() && game.frame_rate == MovieFrameRate{30000, 1001} &&
              game.audio_frames == 4805 &&
              game_bytes == read_bytes(folder / "direct-game.avi", avi_movie_byte_limit),
          "Supervised game movie changed PCM, packets, timebase or receipt");
    rejects([&] { (void)write_avi_game(worker, root, folder, names, 7, 5, 30000. / 1001); },
            "already exists");
    const auto complete = read_bytes(folder / "animation.avi", avi_movie_byte_limit);
    check(info.bytes == complete.size() && info.frame_rate == MovieFrameRate{30000, 1001},
          "Supervised movie output lost its timebase or byte count");
    rejects([&] { (void)write_avi_sequence(worker, root, folder, names, 7, 5, 30); },
            "already exists");
    check(complete == read_bytes(folder / "animation.avi", avi_movie_byte_limit),
          "Rejected movie replacement changed a completed file");
    const auto bad = root / "bad";
    std::filesystem::create_directory(bad);
    for (unsigned i = 0; i < 3; ++i)
        std::filesystem::copy_file(folder / names[i], bad / names[i]);
    asset_storage::replace(bad / names[1], "invalid PNG");
    std::filesystem::copy_file(folder / "audio.s16le", bad / "audio.s16le");
    rejects([&] { (void)write_avi_game(worker, root, bad, names, 7, 5, 30000. / 1001); });
    check(!std::filesystem::exists(bad / "game.avi") &&
              read_bytes(bad / "audio.s16le", avi_game_audio_byte_limit) ==
                  read_bytes(folder / "audio.s16le", avi_game_audio_byte_limit),
          "Failed game worker published a movie or changed completed PCM");
    rejects([&] { (void)write_avi_sequence(worker, root, bad, names, 7, 5, 30); });
    check(!std::filesystem::exists(bad / "animation.avi"),
          "Failed worker published a partial movie");
    check(std::filesystem::is_regular_file(bad / names[0]) &&
              std::filesystem::is_regular_file(bad / names[2]),
          "Failed movie encoding removed completed PNG frames");
    std::stop_source stopped;
    stopped.request_stop();
    rejects(
        [&] {
            (void)write_avi_sequence(worker, root, bad, names, 7, 5, 30, 90, stopped.get_token());
        },
        "cancelled");
    rejects([&] { (void)write_avi_sequence(worker, root, bad, {names.data(), 0}, 7, 5, 30); });
    const std::vector<std::string> escaping{"../0.png"};
    rejects([&] { (void)write_avi_sequence(worker, root, bad, escaping, 7, 5, 30); }, "flat");
    rejects([&] { (void)write_avi_sequence(worker, root, bad, names, 7, 5, 30, 101); }, "quality");
}
} // namespace
int main(int argc, char** argv) {
    try {
        check(argc == 2 || argc == 3, "Need output folder and optional packaged worker");
        const auto root = std::filesystem::absolute(argv[1]) / AssetId::generate().str();
        const auto folder = root / "frames";
        std::filesystem::create_directories(folder);
        for (unsigned i = 0; i < 3; ++i)
            save_image_pixels(folder / (std::to_string(i) + ".png"), picture(i));
        direct(folder);
        game_direct(folder);
        if (argc == 3)
            process(std::filesystem::absolute(argv[2]), root, folder);
        std::cout << "Movie snapshot/codec/failure/cancellation admission PASS: " << root << '\n';
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
