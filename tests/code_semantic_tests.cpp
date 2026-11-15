#include "code_semantic.hpp"
#include "local_file_url.hpp"
#include <iostream>

namespace {
void check(bool value, const char* message) {
    if (!value)
        throw std::runtime_error(message);
}
template <class F> void rejects(F&& action) {
    bool refused = false;
    try {
        action();
    } catch (const std::exception&) {
        refused = true;
    }
    check(refused, "Malformed semantic input was accepted");
}
} // namespace
int main() {
    try {
        const auto path =
            std::filesystem::current_path() / std::filesystem::u8path("é space #%.hpp");
        const auto url = forge::local_file_url(path);
        check(url.find("%C3%A9%20space%20%23%25.hpp") != url.npos &&
                  forge::local_file_url_path(url) == path,
              "File URL lost Unicode, spaces or literal URI punctuation");
        check(forge::local_file_url_path(forge::local_file_url("relative.hpp")) ==
                  std::filesystem::absolute("relative.hpp"),
              "Relative source did not retain its absolute file URL");
        for (const auto* bad :
             {"https://example.com/file.cpp", "file://", "file:///tmp/%", "file:///tmp/%xz",
              "file:///tmp/%00.cpp", "file:///tmp/a?query", "file:///tmp/a#fragment"})
            rejects([&] { (void)forge::local_file_url_path(bad); });
#ifndef _WIN32
        rejects([] { (void)forge::local_file_url_path("file://remote-host/tmp/file.cpp"); });
        check(forge::local_file_url_path("file://localhost/tmp/file.cpp") == "/tmp/file.cpp",
              "Local file authority was refused");
#endif
        const std::string text = "\téx\r\nnext\n";
        check(forge::cpp_semantic_position(text, 3) ==
                      nlohmann::json{{"line", 0}, {"character", 3}} &&
                  forge::cpp_semantic_offset(text, {{"line", 0}, {"character", 3}}) == 3 &&
                  forge::cpp_semantic_offset(text, {{"line", 1}, {"character", 2}}) == 8 &&
                  forge::cpp_semantic_position(text, text.size()) ==
                      nlohmann::json{{"line", 2}, {"character", 0}},
              "Semantic positions did not use UTF-8 bytes and CRLF line bounds");
        rejects([&] { (void)forge::cpp_semantic_position(text, 2); });
        rejects([&] { (void)forge::cpp_semantic_position(text, 5); });
        rejects([&] { (void)forge::cpp_semantic_offset(text, {{"line", 0}, {"character", 2}}); });
        rejects([&] { (void)forge::cpp_semantic_offset(text, {{"line", 0}, {"character", 5}}); });
        rejects([&] { (void)forge::cpp_semantic_offset(text, {{"line", 9}, {"character", 0}}); });
        rejects([&] { (void)forge::cpp_semantic_offset(text, {{"line", -1}, {"character", 0}}); });
        std::cout << "Code semantic file URL checks passed\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
