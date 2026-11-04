#pragma once
#include <stdexcept>
namespace forge {
inline constexpr unsigned runtime_ecs_worker_limit = 16;
inline constexpr unsigned runtime_ecs_worker_profile_version = 1;
// Native fixed-pipeline participation, including the calling owner thread.
// Only systems explicitly marked multi_threaded use worker stages.
struct RuntimeExecutionConfig {
    unsigned ecs_workers = 1;
    void validate() const {
        if (ecs_workers < 1 || ecs_workers > runtime_ecs_worker_limit)
            throw std::invalid_argument("Fixed ECS workers must be in 1..16");
    }
    bool operator==(const RuntimeExecutionConfig&) const = default;
};
} // namespace forge
