#include "asset_storage.hpp"
#include "native_io_path.hpp"
#include <forge/scene.hpp>
#include <fstream>
#include <iostream>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif
using namespace forge;
namespace {
void check(bool ok, const char* why) {
    if (!ok)
        throw std::runtime_error(why);
}
std::string read(const std::filesystem::path& path) {
    std::ifstream stream(path, std::ios::binary);
    check(bool(stream), "Replacement fixture cannot read the selected file");
    const std::string value{std::istreambuf_iterator<char>(stream), {}};
    check(!stream.bad(), "Replacement fixture read failed");
    return value;
}
template <class Write> void held_reader(const std::filesystem::path& path, Write write) {
    const auto before = read(path);
#ifdef _WIN32
    // A real reader permits ordinary reads/writes but not deletion. A legacy
    // MoveFileEx replacement cannot replace this open destination. The native
    // POSIX-rename capability must retain its old snapshot and select the new file.
    const auto file = CreateFileW(asset_detail::native_io_path(path).c_str(), GENERIC_READ,
                                  FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING,
                                  FILE_ATTRIBUTE_NORMAL, nullptr);
    check(file != INVALID_HANDLE_VALUE, "Cannot hold the Windows destination reader");
    std::unique_ptr<void, decltype(&CloseHandle)> reader(file, &CloseHandle);
    write();
    std::string retained(before.size(), '\0');
    DWORD count = 0;
    check(ReadFile(file, retained.data(), DWORD(retained.size()), &count, nullptr) &&
              count == before.size() && retained == before,
          "Windows replacement invalidated the existing reader snapshot");
#else
    std::ifstream reader(path, std::ios::binary);
    check(bool(reader), "Cannot hold the destination reader");
    write();
    check(std::string{std::istreambuf_iterator<char>(reader), {}} == before,
          "Replacement invalidated the existing reader snapshot");
#endif
}
} // namespace
int main(int argc, char** argv) {
    try {
        check(argc == 2, "Need atomic file replacement scratch directory");
        const auto root = std::filesystem::absolute(argv[1]);
        std::filesystem::create_directories(root);
        const std::string before("first\0value", 11), after("updated\0snapshot", 16);
        for (bool metadata : {false, true}) {
            const auto path = root / (metadata ? "metadata.json" : "document.json");
            const auto write = [&](const std::filesystem::path& file, const std::string& value) {
                if (metadata)
                    asset_storage::replace(file, value);
                else
                    atomic_write(file, value);
            };
            write(path, before);
            held_reader(path, [&] { write(path, after); });
            check(read(path) == after, "Replacement did not select exact new bytes");
            const auto blocked = root / (metadata ? "metadata-directory" : "document-directory");
            std::filesystem::create_directories(blocked);
            atomic_write(blocked / "retained", before);
            bool rejected = false;
            try {
                write(blocked, after);
            } catch (const std::exception&) {
                rejected = true;
            }
            check(rejected && read(blocked / "retained") == before,
                  "Failed replacement changed the existing destination");
        }
        EngineContext engine;
        Scene scene(engine.world());
        const auto scene_path = root / "saved.scene.json";
        scene.save(scene_path);
        const auto identity = scene.asset_id();
        auto document = scene.document();
        document["extensions"] = {{"atomic_save_probe", "new saved value"}};
        scene.reset(document);
        held_reader(scene_path, [&] { scene.save(scene_path); });
        Scene reopened(engine.world());
        reopened.load(scene_path);
        check(reopened.asset_id() == identity && reopened.document() == scene.document(),
              "Scene replacement changed identity or saved source payload");
        for (const auto& row : std::filesystem::directory_iterator(root))
            check(row.path().extension() != ".pending", "Replacement leaked a staging file");
        std::cout << "Atomic document/metadata/Scene replacement retains open readers, exact "
                     "bytes, identity and failed-destination contents\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
