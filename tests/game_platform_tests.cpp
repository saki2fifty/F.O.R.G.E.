#include <forge/game_platform.hpp>
#include <forge/game_storage.hpp>
#include <iostream>
int main(int argc, char** argv) {
    try {
        const auto base = forge::game_user_data_base();
        if (argc == 2 && std::string_view(argv[1]) == "--reject")
            throw std::runtime_error("Expected invalid OS environment rejection");
        if (!base.is_absolute() || !std::filesystem::is_directory(base))
            throw std::runtime_error("Invalid OS user directory");
        const auto application = "org.forge.test." + forge::AssetId::generate().str();
        std::filesystem::path owned;
        {
            forge::GameStorage storage(base, application);
            owned = storage.root();
            const auto scene = forge::AssetId::generate();
            forge::GameSaveSchema schema;
            schema.validate = [](const forge::GameSave& save) {
                if (!save.scene || save.data != nlohmann::json{{"score", 7}})
                    throw std::runtime_error("Invalid test game save");
            };
            storage.save("slot1", {scene, {{"score", 7}}}, schema);
            if (storage.load("slot1", schema).scene != scene)
                throw std::runtime_error("OS data save round trip failed");
        }
        // Only this run's unique application directory, after releasing its lease.
        std::filesystem::remove_all(owned);
        std::cout << "OS user-data save round trip passed\n";
        return 0;
    } catch (const std::exception& e) {
        if (argc == 2 && std::string_view(argv[1]) == "--reject" &&
            std::string_view(e.what()).starts_with("game.storage: OS user-data environment"))
            return 0;
        std::cerr << e.what() << '\n';
        return 1;
    }
}
