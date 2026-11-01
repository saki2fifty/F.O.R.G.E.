#include <algorithm>
#include <charconv>
#include <cmath>
#include <forge/application_state_key.hpp>
#include <forge/identity.hpp>
#include <forge/ui_localization.hpp>
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
void validate_localization(const Json& value) {
    require(value.is_object() && value.size() == 3, "Invalid UI localization descriptor");
    require(bool(value.at("entity").get<EntityId>()),
            "UI localization needs a controller identity");
    require(bool(value.at("asset").get<AssetId>()), "UI localization needs a catalog identity");
    require(value.at("locale").is_string(), "UI locale must be a string");
    (void)canonical_ui_locale(value.at("locale").get_ref<const std::string&>());
}
void validate_application_states(const Json& value) {
    require(value.is_object() && value.size() == 6 && value.at("version").is_number_integer() &&
                value.at("version") == 1 && value.at("initialized").is_boolean() &&
                value.at("initialized").get<bool>(),
            "Invalid UI application state descriptor");
    require(value.at("source_asset").is_null() == value.at("controller").is_null(),
            "UI application state source/controller identities must agree");
    for (const auto* key : {"source_asset", "controller"})
        if (!value.at(key).is_null())
            require(key == std::string_view("controller") ? bool(value.at(key).get<EntityId>())
                                                          : bool(value.at(key).get<AssetId>()),
                    "Invalid UI application state identity");
    const auto digest = value.at("definition_digest").get<std::string>();
    require(
        digest.size() == 64 &&
            std::all_of(digest.begin(), digest.end(),
                        [](char c) { return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'); }),
        "Invalid UI application state definition digest");
    const auto& domains = value.at("domains");
    require(domains.is_array() && !domains.empty() && domains.size() <= 16,
            "UI application state domain limit exceeded");
    std::set<std::string> keys;
    for (const auto& domain : domains) {
        require(domain.is_object() && domain.size() == 8, "Invalid UI state observation");
        const auto key = domain.at("key").get<std::string>();
        require(application_state_key(key) && keys.insert(key).second,
                "Invalid or duplicate UI state domain");
        for (const auto* field : {"active", "requested"}) {
            const auto text = domain.at(field).get<std::string>();
            require(text.empty() || application_state_key(text), "Invalid UI state key");
        }
        for (const auto* field : {"label", "active_label"}) {
            validate_value(domain.at(field));
            require(domain.at(field).get_ref<const std::string&>().size() <= 96,
                    "UI application state label exceeds limit");
        }
        (void)natural(domain.at("revision"));
        require(domain.at("computed").is_boolean(), "Invalid UI computed state flag");
        const auto& membership = domain.at("membership");
        require(membership.is_array() && membership.size() <= 16,
                "UI application state hierarchy exceeds limit");
        std::set<std::string> members;
        for (const auto& member : membership) {
            const auto name = member.get<std::string>();
            require(application_state_key(name) && members.insert(name).second,
                    "Invalid UI application state membership");
        }
        const auto active = domain.at("active").get<std::string>();
        require(active.empty() ? members.empty() : members.contains(active),
                "UI application state membership does not match its active state");
    }
    require(value.dump().size() <= 24 * 1024, "UI application state descriptor exceeds limit");
}
Json application_state_request(const std::string& text) {
    require(text.size() <= 118, "UI state request exceeds limit");
    const auto first = text.find(':'), last = text.rfind(':');
    require(first != std::string::npos && last != first,
            "Use domain:state:revision for forge.state");
    const auto domain = text.substr(0, first), state = text.substr(first + 1, last - first - 1);
    require(application_state_key(domain) && application_state_key(state),
            "Invalid UI state request keys");
    std::uint64_t revision = 0;
    const auto parsed =
        std::from_chars(text.data() + last + 1, text.data() + text.size(), revision);
    require(parsed.ec == std::errc{} && parsed.ptr == text.data() + text.size(),
            "UI state request needs an unsigned domain revision");
    return {{"domain", domain}, {"state", state}, {"expected_revision", revision}};
}
void validate_snapshot(const Json& j) {
    scope(j);
    natural(j.at("revision"));
    const bool localized = j.contains("localization") && !j.at("localization").is_null();
    if (localized)
        validate_localization(j.at("localization"));
    const bool states = j.contains("application_states") && !j.at("application_states").is_null();
    if (states)
        validate_application_states(j.at("application_states"));
    const auto& docs = j.at("documents");
    require(docs.is_array() && docs.size() <= max_documents, "UI document limit exceeded");
    require(!localized || !docs.empty(), "UI localization requires an active document");
    require(!states || !docs.empty(), "UI application states require an active document");
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
        require(cmds.is_array() && cmds.size() <= 33 + std::size_t(localized) + std::size_t(states),
                "UI command capability limit exceeded");
        std::set<std::string> unique;
        for (const auto& c : cmds) {
            auto name = c.get<std::string>();
            require(identifier(name) && unique.insert(name).second,
                    "Invalid UI command capability");
        }
        const auto reserved = std::size_t(localized && unique.contains("forge.locale")) +
                              std::size_t(states && unique.contains("forge.state"));
        require(cmds.size() <= 33 + reserved,
                "Extra UI command slots belong to configured locale/state controls");
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
