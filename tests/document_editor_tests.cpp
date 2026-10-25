#include "document_tests.hpp"
#include <iostream>

void require(bool condition, const char* message) {
    if (!condition)
        throw std::runtime_error(message);
}
int main(int argc, char** argv) {
    try {
        if (argc == 2)
            test_document_json_records(argv[1]);
        else
            test_documents();
        std::cout << "Document save, recovery and complete JSON records passed\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
