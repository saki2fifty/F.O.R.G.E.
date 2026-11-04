#pragma once
#include <forge/runtime_execution.hpp>
#include <nlohmann/json.hpp>
namespace forge::detail {
// The same value admission serves project configuration and private recovery.
// It does not own either document or interpret unrelated fields.
inline RuntimeExecutionConfig read_runtime_execution(const nlohmann::json& source) {
    if (!source.is_object())
        throw std::invalid_argument("Runtime execution needs a configuration object");
    RuntimeExecutionConfig result;
    if (const auto value = source.find("ecs_workers"); value != source.end()) {
        if (!value->is_number_integer() || *value < 1 || *value > runtime_ecs_worker_limit)
            throw std::invalid_argument("Fixed ECS workers must be an integer in 1..16");
        result.ecs_workers = value->get<unsigned>();
    }
    result.validate();
    return result;
}
} // namespace forge::detail
