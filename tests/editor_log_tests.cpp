#include "event_time.hpp"
#include <iostream>
#include <stdexcept>

namespace {
constexpr std::size_t limit = 256 * 1024;
void check(bool okay, const char* message) {
    if (!okay)
        throw std::runtime_error(message);
}
std::string prefix(const std::string& text) {
    const auto end = text.find("  ");
    check(end != std::string::npos && end > 0, "Log line has no timestamp");
    return text.substr(0, end + 2);
}
} // namespace

int main() {
    try {
        forge::ui::TimedTextLog log;
        log.append("first");
        const auto time = prefix(log.text());
        log.append(" continued");
        log.append({});
        check(log.text() == time + "first continued", "Partial lines changed their timestamp");
        log.append("\nsecond\n");
        check(log.text().starts_with(time + "first continued\n") &&
                  log.text().ends_with("  second\n"),
              "Normal timestamped output changed");
        log.clear();
        check(log.text().empty(), "Clear retained log text");
        log.append("start:");
        const auto long_time = prefix(log.text());
        const std::string chunk(4096, 'x');
        for (int i = 0; i < 1024; ++i) {
            log.append(chunk);
            check(log.text().size() <= limit, "Unterminated process output exceeded the log cap");
        }
        log.append("LATEST");
        check(log.text().starts_with(long_time) && log.text().ends_with("LATEST") &&
                  log.text().find("[earlier output truncated]") != std::string::npos,
              "An oversized partial line lost its timestamp, truncation marker or newest output");
        log.append("\nafter long line\n");
        check(log.text().ends_with("  after long line\n") && log.text().size() <= limit,
              "Logging did not recover after an oversized line");

        log.clear();
        log.append(std::string(limit * 4, 'z') + "tail\n");
        check(log.text().size() <= limit && log.text().ends_with("tail\n") &&
                  log.text().find("[earlier output truncated]") != std::string::npos,
              "One oversized complete line discarded the newest output");
        log.clear();
        std::string unicode;
        for (std::size_t i = 0; i < limit; ++i)
            unicode += "\xe7\x95\x8c";
        log.append(unicode + "\n");
        const std::string marker = "[earlier output truncated] ";
        const auto start = log.text().find(marker);
        check(log.text().size() <= limit && start != std::string::npos,
              "Unicode output was not bounded");
        auto tail = std::string_view(log.text()).substr(start + marker.size());
        check(tail.ends_with('\n'), "Unicode line lost its ending");
        tail.remove_suffix(1);
        check(!tail.empty() && tail.size() % 3 == 0, "Truncation split a UTF-8 character");
        for (std::size_t i = 0; i < tail.size(); i += 3)
            check(tail.substr(i, 3) == "\xe7\x95\x8c", "Truncation corrupted UTF-8 text");

        log.clear();
        std::string lines;
        for (int i = 0; i < 30000; ++i)
            lines += "line " + std::to_string(i) + "\n";
        log.append(lines);
        check(log.text().size() <= limit && log.text().ends_with("  line 29999\n"),
              "Many complete lines lost the bounded newest output");
        check(log.text().find("[earlier output truncated]") == std::string::npos,
              "Whole-line eviction unnecessarily marked a partial line");
        (void)prefix(log.text());
        log.clear();
        log.append("fresh\n");
        check(log.text() == prefix(log.text()) + "fresh\n", "Clear retained truncation state");
        std::cout << "Timestamped process logs retain bounded recent output and UTF-8 boundaries\n";
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
