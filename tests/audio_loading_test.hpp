#pragma once
#include <chrono>
#include <stdexcept>
#include <thread>
// Explicit tooling preparation; never hide a wait in a game tick or PCM callback.
template <class Preview> void await_audio_preview(Preview& preview) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (preview.pending() || preview.waveform_pending()) {
        preview.poll();
        if (std::chrono::steady_clock::now() >= deadline)
            throw std::runtime_error("Audio preview preparation timed out");
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
}
