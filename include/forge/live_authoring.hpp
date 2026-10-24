#pragma once
#include <forge/authoring.hpp>
#include <functional>
#include <memory>
#include <vector>
namespace forge {
// Registered by the existing editor owner for an explicitly granted workflow.
// Schemas describe method-specific fields; target/revision/sequence are shared.
struct LiveAuthoringMethod {
    std::string name, description, capability;
    Json input_schema;
    bool mutation = false;
    std::function<Json(const Json&)> invoke;
    // Only owner-validated progress/cancel/finish controls may bypass a general
    // editor busy message. Scene edits and new job admissions keep the guard.
    bool controls_pending_work = false;
    std::string failure_code = "document_error";
    // Optional native result projection for protocol adapters, never a file path.
    std::string result_content = {};
    // Protocol hint only; permission checks stay in the owning workflow.
    bool executes_native = false;
};
// Explicitly enabled, loopback-only JSON-lines adapter. All scene access occurs
// in pump() on the owning thread. Transport identity is independent of API identity.
class LiveAuthoring {
  public:
    LiveAuthoring();
    ~LiveAuthoring();
    LiveAuthoring(const LiveAuthoring&) = delete;
    LiveAuthoring& operator=(const LiveAuthoring&) = delete;
    void start(Scene& scene, bool allow_edits, std::vector<LiveAuthoringMethod> methods = {});
    // Refresh scene routing while retaining the connection's sequence receipts.
    // Only a granted project-document owner may keep a connection across scenes.
    void retarget(Scene& scene);
    void stop();
    void pump(const std::string& unavailable_reason = {});
    bool active() const;
    bool editable() const;
    std::uint16_t port() const;
    Json connection() const;
    const std::string& status() const;

  private:
    struct State;
    std::unique_ptr<State> state_;
};
} // namespace forge
