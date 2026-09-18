#include <cmath>
#include <forge/identity.hpp>
#include <forge/ui_protocol.hpp>
#include <set>
#include <stdexcept>
namespace forge::ui_protocol {
namespace {
void require(bool b, const char* why) {
    if (!b)
        throw std::runtime_error(why);
}
std::uint64_t natural(const Json& v) {
    require(v.is_number_unsigned(), "UI protocol requires an unsigned integer");
    return v.get<std::uint64_t>();
}
void scope(const Json& j) {
    require(j.is_object() && j.value("version", 0) == version, "Unsupported private UI protocol");
    const auto s = j.at("session").get<std::string>();
    require(!s.empty() && s.size() <= 128, "Invalid UI session");
    require(natural(j.at("generation")) > 0, "Invalid UI generation");
}
} // namespace
bool identifier(const std::string& s) {
    if (s.empty() || s.size() > 64)
        return false;
    for (unsigned char c : s)
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
              c == '_' || c == '.'))
            return false;
    return true;
}
void validate_value(const Json& v) {
    if (v.is_boolean())
        return;
    if (v.is_number()) {
        require(std::isfinite(v.get<double>()), "Nonfinite UI value");
        return;
    }
    require(v.is_string() && v.get_ref<const std::string&>().size() <= max_string,
            "UI values require bounded scalar types");
    const auto& s = v.get_ref<const std::string&>();
    require(s.find('\0') == std::string::npos, "UI text contains NUL");
    (void)v.dump(); // strict UTF-8 validation
}
void validate_model(const Json& j) {
    require(j.is_object() && j.size() <= max_values, "UI model exceeds scalar count limit");
    for (const auto& [k, v] : j.items()) {
        require(identifier(k), "Invalid UI variable name");
        validate_value(v);
    }
}
void validate_snapshot(const Json& j) {
    scope(j);
    natural(j.at("revision"));
    const auto& docs = j.at("documents");
    require(docs.is_array() && docs.size() <= max_documents, "UI document limit exceeded");
    std::set<std::string> ids;
    for (const auto& d : docs) {
        const auto entity = d.at("entity").get<std::string>();
        (void)EntityId::parse(entity);
        (void)d.at("asset").get<AssetId>();
        const auto instance = d.at("instance").get<std::string>();
        require(!instance.empty() && instance.size() <= 128 && ids.insert(instance).second,
                "Invalid UI document instance");
        require(d.at("visible").is_boolean() && natural(d.at("layer")) <= 255,
                "Invalid UI display options");
        validate_model(d.at("model"));
        const auto& cmds = d.at("commands");
        require(cmds.is_array() && cmds.size() <= 32, "UI command capability limit exceeded");
        std::set<std::string> unique;
        for (const auto& c : cmds) {
            auto name = c.get<std::string>();
            require(identifier(name) && unique.insert(name).second,
                    "Invalid UI command capability");
        }
    }
    require(j.dump().size() <= 65536, "UI snapshot exceeds 64 KiB");
}
void Replica::reset(std::string session, std::uint64_t generation) {
    session_ = std::move(session);
    generation_ = generation;
    revision_ = 0;
    value_ = nullptr;
}
bool Replica::accept(const Json& j) {
    validate_snapshot(j);
    if (j.at("session") != session_ || j.at("generation") != generation_)
        return false;
    const auto revision = natural(j.at("revision"));
    if (!value_.is_null() && revision <= revision_)
        return false;
    value_ = j;
    revision_ = revision;
    return true;
}
void CommandGate::reset(std::string session, std::uint64_t generation) {
    session_ = std::move(session);
    generation_ = generation;
    last_ = 0;
    receipts_.clear();
}
Json CommandGate::dispatch(const Json& j, const std::function<void(const Json&)>& action) {
    scope(j);
    require(j.dump().size() <= 4096, "UI command exceeds 4 KiB");
    require(j.at("session") == session_ && j.at("generation") == generation_,
            "Stale UI session/generation");
    const auto id = natural(j.at("id"));
    require(id > 0, "UI command ID must be positive");
    if (auto it = receipts_.find(id); it != receipts_.end()) {
        require(it->second.request == j, "UI command ID reused with different content");
        return it->second.ack;
    }
    require(id > last_, "UI command receipt expired; it will not execute again");
    require(id == last_ + 1, "UI command sequence gap");
    require(identifier(j.at("command").get<std::string>()), "Invalid UI command name");
    const auto instance = j.at("instance").get<std::string>();
    require(!instance.empty() && instance.size() <= 128, "Invalid UI command document instance");
    if (j.contains("value"))
        validate_value(j.at("value"));
    Json ack = {{"version", version},
                {"session", session_},
                {"generation", generation_},
                {"id", id},
                {"ok", false}};
    // Allocate the receipt before invoking anything that may change authoritative state.
    auto [it, inserted] = receipts_.emplace(id, Receipt{j, ack});
    (void)inserted;
    last_ = id;
    try {
        action(j);
        it->second.ack["ok"] = true;
    } catch (const std::exception& e) {
        it->second.ack["error"] = std::string(e.what()).substr(0, 1024);
    }
    while (receipts_.size() > 64)
        receipts_.erase(receipts_.begin());
    return it->second.ack;
}
} // namespace forge::ui_protocol
