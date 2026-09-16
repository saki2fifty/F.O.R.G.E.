#include <chrono>
#include <forge/live_authoring.hpp>
#include <forge/project_lease.hpp>
#include <iostream>
#include <thread>
int main(int argc, char** argv) {
    try {
        if (argc == 3 && std::string(argv[1]) == "--lease") {
            forge::ProjectLease lease(std::filesystem::u8path(argv[2]));
            std::cout << "owned" << std::endl;
            std::cin.get();
            lease.check();
            return 0;
        }
        if (argc != 2)
            return 2;
        forge::Scene scene;
        forge::LiveAuthoring live;
        live.start(scene, std::string(argv[1]) != "read");
        std::cout << live.connection().dump() << std::endl;
        const auto end = std::chrono::steady_clock::now() + std::chrono::seconds(60);
        while (std::chrono::steady_clock::now() < end) {
            live.pump(std::string(argv[1]) == "busy" ? "Test gesture active" : "");
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        return 0;
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
