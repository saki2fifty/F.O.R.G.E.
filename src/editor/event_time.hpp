#pragma once
#include <chrono>
#include <cstddef>
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
        line_time_.clear();
        line_start_ = true;
    }
    void append(std::string_view chunk) {
        if (chunk.empty())
            return;
        const auto received = event_time_local();
        for (const char c : chunk) {
            if (line_start_) {
                line_time_ = received;
                text_ += received;
                text_ += "  ";
                line_start_ = false;
            }
            text_ += c;
            if (c == '\n')
                line_start_ = true;
            // Bound temporary growth too, including a single oversized append.
            // Trimming in batches avoids shifting the whole tail for each byte.
            if (text_.size() > 2 * maximum_bytes)
                trim();
        }
        trim();
    }
    const std::string& text() const { return text_; }

  private:
    static constexpr std::size_t maximum_bytes = 256 * 1024;
    void trim() {
        if (text_.size() <= maximum_bytes)
            return;
        const auto cut = text_.find('\n', text_.size() - maximum_bytes - 1);
        if (cut != std::string::npos && cut + 1 < text_.size()) {
            text_.erase(0, cut + 1);
            return;
        }
        // The newest line alone exceeds the budget. Keep its original timestamp
        // and recent tail, even when the process never emits a newline.
        const auto prefix = line_time_ + "  [earlier output truncated] ";
        auto start = text_.size() - (maximum_bytes - prefix.size());
        while (start < text_.size() && (static_cast<unsigned char>(text_[start]) & 0xc0) == 0x80)
            ++start;
        text_.erase(0, start);
        text_.insert(0, prefix);
    }
    std::string text_, line_time_;
    bool line_start_ = true;
};
} // namespace forge::ui
