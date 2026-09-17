#include <chrono>
#include <iostream>
#include <nlohmann/json.hpp>
#include <string>
#include <thread>
int main() {
    std::string line;
    std::getline(std::cin, line);
    auto request = nlohmann::json::parse(line);
    std::cout << nlohmann::json({{"protocol", 2},
                                 {"session", "fault"},
                                 {"id", request.at("id")},
                                 {"ok", true},
                                 {"scene", {{"version", 3}, {"entities", nlohmann::json::array()}}},
                                 {"effective_scene", nlohmann::json::object()},
                                 {"timing", {{"paused", true}}},
                                 {"activation", {{"state", "none"}}}})
                     .dump()
              << std::endl;
    std::getline(std::cin, line);
    request = nlohmann::json::parse(line);
    std::cerr << "fault-worker diagnostic" << std::flush;
    const auto mode = request.at("scene").at("test_failure");
    if (mode == "stale_session" || mode == "stale_id") {
        auto response = nlohmann::json{
            {"protocol", 2}, {"id", request.at("id")}, {"session", "fault"}, {"ok", true}};
        if (mode == "stale_session")
            response["session"] = "old-fault-session";
        else
            response["id"] = 0;
        std::cout << response.dump() << std::endl;
        std::this_thread::sleep_for(std::chrono::seconds(30));
    }
    if (mode == "hang")
        std::this_thread::sleep_for(std::chrono::seconds(30));
    if (mode == "malformed") {
        std::cout << "invalid response\n" << std::flush;
        std::this_thread::sleep_for(std::chrono::seconds(30));
    }
    return 42;
}
