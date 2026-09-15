#include <chrono>
#include <iostream>
#include <nlohmann/json.hpp>
#include <string>
#include <thread>
int main() {
    std::string line;
    std::getline(std::cin, line);
    auto request = nlohmann::json::parse(line);
    std::cerr << "fault-worker diagnostic" << std::flush;
    const auto mode = request.at("scene").at("test_failure");
    if (mode == "hang")
        std::this_thread::sleep_for(std::chrono::seconds(30));
    if (mode == "malformed") {
        std::cout << "invalid response\n" << std::flush;
        std::this_thread::sleep_for(std::chrono::seconds(30));
    }
    return 42;
}
