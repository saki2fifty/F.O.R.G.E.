#include "game_launch.hpp"
#include <iostream>
#include <stdexcept>
#include <vector>
using namespace forge;
void check(bool value, const char* message) {
    if (!value)
        throw std::runtime_error(message);
}
int main() {
    try {
        auto parse = [](std::initializer_list<std::string_view> arguments) {
            return parse_game_launch({arguments.begin(), arguments.size()});
        };
        const auto normal = parse({});
        check(!normal.project && !normal.verify_startup && !normal.show_fps,
              "Default launch changed distribution or diagnostic policy");
        const auto fps = parse({"--show-fps"});
        check(!fps.project && !fps.verify_startup && fps.show_fps,
              "FPS option changed distribution or physical/software policy");
        const auto verified = parse({"--show-fps", "--verify-startup"});
        check(verified.show_fps && verified.verify_startup && !verified.project,
              "Diagnostic options did not compose");
        for (const auto& arguments :
             {std::vector<std::string_view>{"--show-fps", "--project", "Folder/\xc3\xa9/game"},
              std::vector<std::string_view>{"--project", "Folder/\xc3\xa9/game", "--show-fps"}}) {
            const auto launch = parse_game_launch(arguments);
            check(launch.project == "Folder/\xc3\xa9/game" && launch.show_fps &&
                      !launch.verify_startup,
                  "Development project locator or flag ordering changed");
        }
        for (const auto& arguments :
             {std::vector<std::string_view>{"--project"},
              std::vector<std::string_view>{"--project", ""},
              std::vector<std::string_view>{"--project", "--show-fps"},
              std::vector<std::string_view>{"--unknown"},
              std::vector<std::string_view>{"positional"},
              std::vector<std::string_view>{"--project", "a", "extra"},
              std::vector<std::string_view>{"--project", "a", "--project", "b"},
              std::vector<std::string_view>{"--verify-startup", "--project", "a"}}) {
            bool refused = false;
            try {
                (void)parse_game_launch(arguments);
            } catch (const std::exception&) {
                refused = true;
            }
            check(refused, "Invalid launch arguments were accepted");
        }
        std::cout << "Standalone defaults, FPS/verification modes, development locators and "
                     "invalid launch arguments passed\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
