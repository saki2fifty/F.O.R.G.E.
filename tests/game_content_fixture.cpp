#include "game_host_source.hpp"
#include <iostream>
int main(int argc, char** argv) {
    try {
        if (argc != 2)
            throw std::runtime_error("Need fixture output directory");
        std::cout << forge::path_utf8(
                         forge::test::make_game_host_project(std::filesystem::absolute(argv[1])))
                  << '\n';
        return 0;
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
