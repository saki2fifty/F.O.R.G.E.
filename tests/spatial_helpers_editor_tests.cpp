#include <forge/content_digest.hpp>
#include <stdexcept>
void require(bool value, const char* message) {
    if (!value)
        throw std::runtime_error(message);
}
#include "spatial_helpers_tests.hpp"
#include <iostream>
int main() {
    try {
        test_spatial_helpers();
        require(forge::valid_content_digest(std::string(64, 'a')),
                "Shared digest predicate rejected lowercase SHA256");
        for (const auto& invalid :
             {std::string{}, std::string(63, 'a'), std::string(65, 'a'), std::string(64, 'A'),
              std::string(64, 'g'), std::string(64, '\0')})
            require(!forge::valid_content_digest(invalid),
                    "Shared digest predicate admitted invalid text");
        std::cout << "Existing Camera/Light helper geometry, policies, cache and picking passed\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
