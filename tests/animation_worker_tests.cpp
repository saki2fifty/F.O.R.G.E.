#include "animation_worker.hpp"
#include <chrono>
#include <forge/identity.hpp>
#include <fstream>
#include <iostream>
#include <thread>
int main(int argc, char** argv) {
    try {
        if (argc != 3)
            throw std::runtime_error("Need child and scratch path");
        const auto child = std::filesystem::absolute(argv[1]);
        const auto root = std::filesystem::path(argv[2]) / forge::AssetId::generate().str();
        std::filesystem::create_directories(root);
        struct Cleanup {
            std::filesystem::path p;
            ~Cleanup() {
                std::error_code e;
                std::filesystem::remove_all(p, e);
            }
        } cleanup{root};
        auto run = [&](const char* mode, bool rejected, std::stop_token token = {}) {
            std::ofstream(root / "source.gltf") << mode;
            bool failed = false;
            try {
                forge::animation_detail::run_converter(child, root, token);
            } catch (const std::exception&) {
                failed = true;
            }
            if (failed != rejected)
                throw std::runtime_error(std::string("Worker policy mismatch: ") + mode);
        };
        run("limits", false);
        run("fail", true);
        run("size", true);
        std::filesystem::remove(root / "oversize.bin");
        std::stop_source cancel;
        std::jthread requester([&] {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            cancel.request_stop();
        });
        auto start = std::chrono::steady_clock::now();
        run("delay", true, cancel.get_token());
        if (std::chrono::steady_clock::now() - start > std::chrono::seconds(3))
            throw std::runtime_error("Worker cancellation was not prompt");
        start = std::chrono::steady_clock::now();
        run("delay", true);
        auto elapsed = std::chrono::steady_clock::now() - start;
        if (elapsed < std::chrono::seconds(29) || elapsed > std::chrono::seconds(35))
            throw std::runtime_error("Worker timeout was not enforced");
        std::cout << "Converter memory/file policy, failure, cancellation and timeout passed\n";
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
