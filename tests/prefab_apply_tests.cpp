#include "prefab_apply_editor_tests.hpp"
#include <iostream>
void require(bool condition, const char* message) {
    if (!condition)
        throw std::runtime_error(message);
}
int main() {
    try {
        test_prefab_apply_documents();
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
    std::cout << "Coordinated prefab Apply document/history tests passed\n";
}
