#pragma once
#include <algorithm>
#include <stdexcept>
#include <string>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <cerrno>
#include <csignal>
#include <fcntl.h>
#include <unistd.h>
#endif
namespace forge {
// Bounded polling on inherited process pipes. No background threads or ECS access.
class RuntimeIo {
  public:
    static constexpr std::size_t limit = 16 * 1024 * 1024;
    RuntimeIo() {
#ifdef _WIN32
        input_ = GetStdHandle(STD_INPUT_HANDLE);
        output_ = GetStdHandle(STD_OUTPUT_HANDLE);
        for (auto handle : {input_, output_}) {
            if (GetFileType(handle) != FILE_TYPE_PIPE)
                throw std::runtime_error("Runtime control requires redirected stdin/stdout pipes");
            DWORD mode = PIPE_NOWAIT;
            if (!SetNamedPipeHandleState(handle, &mode, nullptr, nullptr))
                throw std::runtime_error("Cannot configure runtime nonblocking pipe: " +
                                         std::to_string(GetLastError()));
        }
#else
        std::signal(SIGPIPE, SIG_IGN);
        for (int fd : {STDIN_FILENO, STDOUT_FILENO}) {
            const int mode = fcntl(fd, F_GETFL);
            if (mode < 0 || fcntl(fd, F_SETFL, mode | O_NONBLOCK) < 0)
                throw std::runtime_error("Cannot configure runtime nonblocking pipe");
        }
#endif
    }
    bool pending() const { return !outgoing_.empty(); }
    bool closed() const { return closed_; }
    bool receive(std::string& line) {
        char bytes[8192];
        for (unsigned i = 0; i < 8 && incoming_.find('\n') == std::string::npos; ++i) {
            std::size_t count = 0;
#ifdef _WIN32
            DWORD read = 0;
            if (!ReadFile(input_, bytes, sizeof(bytes), &read, nullptr)) {
                const auto error = GetLastError();
                if (error == ERROR_NO_DATA)
                    break;
                if (error == ERROR_BROKEN_PIPE) {
                    closed_ = true;
                    break;
                }
                throw std::runtime_error("Runtime read failed: " + std::to_string(error));
            }
            count = read;
#else
            const auto read = ::read(STDIN_FILENO, bytes, sizeof(bytes));
            if (read < 0) {
                if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR)
                    break;
                throw std::runtime_error("Runtime read failed");
            }
            count = static_cast<std::size_t>(read);
#endif
            if (!count) {
                closed_ = true;
                break;
            }
            incoming_.append(bytes, count);
            if (incoming_.size() > limit)
                throw std::runtime_error("Runtime request exceeds 16 MiB");
        }
        const auto end = incoming_.find('\n');
        if (end == std::string::npos)
            return false;
        line = incoming_.substr(0, end);
        incoming_.erase(0, end + 1);
        return true;
    }
    void send(std::string bytes) {
        if (pending())
            throw std::runtime_error("Runtime response already pending");
        if (bytes.size() > limit)
            throw std::runtime_error("Runtime response exceeds 16 MiB");
        outgoing_ = std::move(bytes);
        offset_ = 0;
    }
    void flush() {
        // Both peers may poll. Keep writes below the default anonymous-pipe quota;
        // a larger nonblocking write can make no progress without a pending reader.
        for (unsigned i = 0; pending() && i < 32; ++i) {
            const auto size = std::min<std::size_t>(1024, outgoing_.size() - offset_);
            std::size_t count = 0;
#ifdef _WIN32
            DWORD written = 0;
            if (!WriteFile(output_, outgoing_.data() + offset_, static_cast<DWORD>(size), &written,
                           nullptr)) {
                const auto error = GetLastError();
                if (error == ERROR_NO_DATA || error == ERROR_BROKEN_PIPE) {
                    closed_ = true;
                    break;
                }
                throw std::runtime_error("Runtime write failed: " + std::to_string(error));
            }
            count = written;
#else
            const auto written = ::write(STDOUT_FILENO, outgoing_.data() + offset_, size);
            if (written < 0) {
                if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR)
                    break;
                if (errno == EPIPE) {
                    closed_ = true;
                    break;
                }
                throw std::runtime_error("Runtime write failed");
            }
            count = static_cast<std::size_t>(written);
#endif
            if (!count)
                break;
            offset_ += count;
            if (offset_ == outgoing_.size()) {
                outgoing_.clear();
                offset_ = 0;
            }
        }
    }

  private:
    std::string incoming_, outgoing_;
    std::size_t offset_ = 0;
    bool closed_ = false;
#ifdef _WIN32
    HANDLE input_, output_;
#endif
};
} // namespace forge
