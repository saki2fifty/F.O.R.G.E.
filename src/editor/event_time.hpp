#pragma once
#include <chrono>
#include <ctime>
#include <string>
#include <string_view>

namespace forge::ui {
inline std::string event_time_local() {
    const auto now = std::chrono::system_clock::now();
    const std::time_t seconds = std::chrono::system_clock::to_time_t(now);
    std::tm local{};
#ifdef _WIN32
    if (localtime_s(&local, &seconds) != 0)
        return "Time unavailable";
#else
    if (!localtime_r(&seconds, &local))
        return "Time unavailable";
#endif
    char text[48]{};
    if (!std::strftime(text, sizeof(text), "%Y-%m-%d %H:%M:%S %z", &local))
        return "Time unavailable";
    return text;
}
// Presentation-only copy. Compiler/runtime parsers keep their original raw text.
class TimedTextLog {
  public:
    void clear() {
        text_.clear();
        line_start_ = true;
    }
    void append(std::string_view chunk) {
        if (chunk.empty())
            return;
        const auto received = event_time_local();
        for (const char c : chunk) {
            if (line_start_) {
                text_ += received;
                text_ += "  ";
                line_start_ = false;
            }
            text_ += c;
            if (c == '\n')
                line_start_ = true;
        }
        if (text_.size() > 256 * 1024) {
            const auto cut = text_.find('\n', text_.size() - 256 * 1024);
            if (cut != std::string::npos)
                text_.erase(0, cut + 1);
        }
    }
    const std::string& text() const { return text_; }

  private:
    std::string text_;
    bool line_start_ = true;
};
} // namespace forge::ui
