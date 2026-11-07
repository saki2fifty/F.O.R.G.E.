#include "sound_cue_timing_runtime_tests.hpp"

int main(int argc, char** argv) {
    try {
        check(argc == 2, "Test path required");
        const auto root = std::filesystem::path(argv[1]) / AssetId::generate().str();
        std::filesystem::create_directories(root / "Assets");
        wav(root / "Assets/tone.wav");
        const auto record = AssetCatalog::register_audio_clip(root, "Assets/tone.wav");
        sound_cue_timing_runtime_tests(root, record.id);
        std::cout << "Timed cue PCM/pitch/pause/stop/routing checks passed\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
