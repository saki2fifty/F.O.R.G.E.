#pragma once
#include <atomic>
#include <chrono>
#include <miniaudio.h>
#include <stdexcept>
#include <thread>
namespace forge::test {
struct AudioStreamVfs {
    inline static thread_local bool consuming = false;
    ma_vfs_callbacks callbacks{};
    ma_default_vfs disk{};
    std::thread::id owner = std::this_thread::get_id();
    std::atomic<unsigned> owner_io{0}, consumer_io{0}, worker_io{0};
    std::atomic<unsigned> delay_read_ms{0};
    std::atomic<bool> fail_reads{false};
    void hit() {
        if (consuming)
            ++consumer_io;
        if (std::this_thread::get_id() == owner)
            ++owner_io;
        else
            ++worker_io;
    }
    static AudioStreamVfs& self(ma_vfs* v) {
        auto& s = *static_cast<AudioStreamVfs*>(v);
        s.hit();
        return s;
    }
    AudioStreamVfs() {
        if (ma_default_vfs_init(&disk, nullptr) != MA_SUCCESS)
            throw std::runtime_error("Audio fixture VFS initialization failed");
        callbacks.onOpen = [](ma_vfs* v, const char* p, ma_uint32 m, ma_vfs_file* f) {
            return ma_vfs_open(&self(v).disk, p, m, f);
        };
        callbacks.onOpenW = [](ma_vfs* v, const wchar_t* p, ma_uint32 m, ma_vfs_file* f) {
            return ma_vfs_open_w(&self(v).disk, p, m, f);
        };
        callbacks.onClose = [](ma_vfs* v, ma_vfs_file f) { return ma_vfs_close(&self(v).disk, f); };
        callbacks.onRead = [](ma_vfs* v, ma_vfs_file f, void* p, size_t n, size_t* r) {
            auto& fixture = self(v);
            if (const auto delay = fixture.delay_read_ms.exchange(0))
                std::this_thread::sleep_for(std::chrono::milliseconds(delay));
            if (fixture.fail_reads) {
                if (r)
                    *r = 0;
                return MA_IO_ERROR;
            }
            return ma_vfs_read(&fixture.disk, f, p, n, r);
        };
        callbacks.onWrite = [](ma_vfs* v, ma_vfs_file f, const void* p, size_t n, size_t* r) {
            return ma_vfs_write(&self(v).disk, f, p, n, r);
        };
        callbacks.onSeek = [](ma_vfs* v, ma_vfs_file f, ma_int64 n, ma_seek_origin p) {
            return ma_vfs_seek(&self(v).disk, f, n, p);
        };
        callbacks.onTell = [](ma_vfs* v, ma_vfs_file f, ma_int64* n) {
            return ma_vfs_tell(&self(v).disk, f, n);
        };
        callbacks.onInfo = [](ma_vfs* v, ma_vfs_file f, ma_file_info* i) {
            return ma_vfs_info(&self(v).disk, f, i);
        };
    }
};
} // namespace forge::test
