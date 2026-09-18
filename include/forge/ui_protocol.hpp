#pragma once
#include <deque>
#include <functional>
#include <map>
#include <nlohmann/json.hpp>
#include <string>
namespace forge::ui_protocol {
using Json = nlohmann::json;
inline constexpr unsigned version = 1;
inline constexpr std::size_t max_documents = 16, max_values = 32, max_string = 1024;
bool identifier(const std::string&);
void validate_value(const Json&);
void validate_model(const Json&);
void validate_snapshot(const Json&);
// A session is assigned by the runtime. Generation changes on successful world replacement.
// The presentation host must explicitly reset trust on Play/replacement, never accept an
// unsolicited session merely because an arriving snapshot names it.
class Replica {
  public:
    void reset(std::string session, std::uint64_t generation);
    bool accept(const Json& snapshot);
    const Json& snapshot() const { return value_; }

  private:
    std::string session_;
    std::uint64_t generation_ = 0, revision_ = 0;
    Json value_;
};
class CommandGate {
  public:
    void reset(std::string session, std::uint64_t generation);
    // Action runs at most once, including failed actions. Exact retries return the stored ack.
    Json dispatch(const Json&, const std::function<void(const Json&)>& action);

  private:
    struct Receipt {
        Json request, ack;
    };
    std::string session_;
    std::uint64_t generation_ = 0, last_ = 0;
    std::map<std::uint64_t, Receipt> receipts_;
};
} // namespace forge::ui_protocol
