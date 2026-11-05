#pragma once
#include <memory>
#include <thread>

namespace forge::asset_detail {
// Only detached native data-source and immutable temporary-file destruction.
// The audio provider joins this worker before its native resource manager exits.
class AudioRetirement {
    struct State;

  public:
    class Sink {
      public:
        Sink() = default;
        void retire(void*, void (*destroy)(void*) noexcept) const noexcept;
        unsigned pending() const noexcept;

      private:
        std::shared_ptr<State> state_;
        explicit Sink(std::shared_ptr<State>);
        friend class AudioRetirement;
    };
    AudioRetirement();
    ~AudioRetirement();
    AudioRetirement(const AudioRetirement&) = delete;
    AudioRetirement& operator=(const AudioRetirement&) = delete;
    Sink sink() const;
    void close();

  private:
    std::shared_ptr<State> state_;
    std::thread worker_;
};
} // namespace forge::asset_detail
