#pragma once
#include <forge/world.hpp>
#include <memory>
namespace forge {
// Owner-thread inspection. Does not replace authored persistence or execute gameplay.
class EcsTools {
  public:
    explicit EcsTools(WorldContext& context);
    ~EcsTools();
    EcsTools(const EcsTools&) = delete;
    EcsTools& operator=(const EcsTools&) = delete;
    Json query(const std::string& expression, unsigned offset = 0, unsigned limit = 100);
    Json entity(ecs_entity_t id) const;
    Json statistics();
    std::string export_world() const;
    void sample(float elapsed);
    Json alerts() const;
    ecs_entity_t create_metric(ecs_entity_t source, const std::string& kind, bool member);
    void remove_metric(ecs_entity_t metric);
    Json metrics() const;
    void start_rest(unsigned port);
    void stop_rest();
    void poll_rest(float elapsed);
    unsigned rest_port() const;
    // Same admission path as HTTP, useful for headless integration tests.
    Json rest_request(const std::string& method, const std::string& path);

  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace forge
