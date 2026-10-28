#include <algorithm>
#include <cmath>
#include <forge/input.hpp>
#include <initializer_list>
#include <set>
#include <stdexcept>
namespace forge {
using Json = nlohmann::json;
namespace {
void gamepad_record(const Json& value, std::initializer_list<std::string_view> required,
                    std::initializer_list<std::string_view> optional = {}) {
    if (!value.is_object())
        throw std::runtime_error("Gamepad data must be an object");
    for (auto name : required)
        if (!value.contains(std::string(name)))
            throw std::runtime_error("Missing gamepad field: " + std::string(name));
    for (auto field = value.begin(); field != value.end(); ++field)
        if (std::find(required.begin(), required.end(), field.key()) == required.end() &&
            std::find(optional.begin(), optional.end(), field.key()) == optional.end())
            throw std::runtime_error("Unknown gamepad field: " + field.key());
}
bool gamepad_token(std::string_view value) {
    return !value.empty() && value.size() <= 128 &&
           std::all_of(value.begin(), value.end(), [](unsigned char c) {
               return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
                      c == '-' || c == '_' || c == ':' || c == '.';
           });
}
std::uint32_t gamepad_slot_value(const Json& value) {
    if (!value.is_number_integer() || value < 0 || value > gamepad_slot_limit)
        throw std::runtime_error("Gamepad slot must be an integer in 0..4");
    return value.get<std::uint32_t>();
}
std::uint64_t gamepad_unsigned(const Json& value, const char* field = "revision") {
    if (!value.is_number_integer() ||
        (!value.is_number_unsigned() && value.get<std::int64_t>() < 0))
        throw std::runtime_error(std::string("Gamepad ") + field + " must be an unsigned integer");
    return value.get<std::uint64_t>();
}
Json sensor_sample_json(const GamepadSensorSample& sample) {
    return {{"supported", sample.supported},
            {"enabled", sample.enabled},
            {"measured", sample.measured},
            {"rate_hz", sample.rate_hz},
            {"timestamp_ns", sample.timestamp_ns},
            {"sensor_timestamp_ns", sample.sensor_timestamp_ns},
            {"value", sample.value}};
}
GamepadSensorSample read_sensor_sample(const Json& value) {
    gamepad_record(value, {"supported", "enabled", "measured", "rate_hz", "timestamp_ns",
                           "sensor_timestamp_ns", "value"});
    if (!value.at("supported").is_boolean() || !value.at("enabled").is_boolean() ||
        !value.at("measured").is_boolean() || !value.at("rate_hz").is_number() ||
        !value.at("value").is_array() || value.at("value").size() != 3)
        throw std::runtime_error("Invalid gamepad sensor sample record");
    GamepadSensorSample sample;
    sample.supported = value.at("supported").get<bool>();
    sample.enabled = value.at("enabled").get<bool>();
    sample.measured = value.at("measured").get<bool>();
    sample.rate_hz = value.at("rate_hz").get<double>();
    sample.timestamp_ns = gamepad_unsigned(value.at("timestamp_ns"), "timestamp_ns");
    sample.sensor_timestamp_ns =
        gamepad_unsigned(value.at("sensor_timestamp_ns"), "sensor_timestamp_ns");
    for (std::size_t i = 0; i < sample.value.size(); ++i) {
        if (!value.at("value")[i].is_number())
            throw std::runtime_error("Gamepad sensor values must be numeric SI coordinates");
        sample.value[i] = value.at("value")[i].get<double>();
    }
    return sample;
}
} // namespace
void GamepadAssignment::validate() const {
    if (!revision || !gamepad_token(device) || slot > gamepad_slot_limit)
        throw std::runtime_error(
            "Gamepad assignment needs a current device/revision and slot 0..4");
}
void to_json(Json& json, const GamepadAssignment& value) {
    value.validate();
    json = {{"revision", value.revision}, {"device", value.device}, {"slot", value.slot}};
}
void from_json(const Json& json, GamepadAssignment& value) {
    gamepad_record(json, {"revision", "device", "slot"}, {"operation"});
    if (json.contains("operation") && json.at("operation") != "gamepad_assign")
        throw std::runtime_error("Unsupported gamepad assignment operation");
    if (!json.at("device").is_string() ||
        json.at("device").get_ref<const std::string&>().size() > 128)
        throw std::runtime_error("Gamepad device must be a bounded connection token");
    GamepadAssignment candidate{gamepad_unsigned(json.at("revision")),
                                json.at("device").get<std::string>(),
                                gamepad_slot_value(json.at("slot"))};
    candidate.validate();
    value = std::move(candidate);
}
void GamepadTopology::validate() const {
    if (devices.size() > gamepad_device_limit || (!devices.empty() && !revision) ||
        (!available && (revision || !active.empty() || !devices.empty())))
        throw std::runtime_error("Invalid gamepad host observation");
    std::set<std::string> tokens;
    std::set<std::uint32_t> slots;
    for (const auto& device : devices) {
        if (!gamepad_token(device.device) || !tokens.insert(device.device).second ||
            device.slot > gamepad_slot_limit ||
            (device.slot && !slots.insert(device.slot).second) || device.name.empty() ||
            device.name.size() > 256 ||
            std::any_of(device.name.begin(), device.name.end(),
                        [](unsigned char c) { return c < 32 || c == 127; }))
            throw std::runtime_error("Invalid gamepad device observation");
        (void)Json(device.name).dump(); // Validate UTF-8 using the existing JSON library.
    }
    if (!active.empty() && !tokens.contains(active))
        throw std::runtime_error("Active gamepad is absent from the host observation");
}
void to_json(Json& json, const GamepadTopology& value) {
    value.validate();
    Json devices = Json::array();
    for (const auto& device : value.devices)
        devices.push_back({{"device", device.device},
                           {"name", device.name},
                           {"slot", device.slot},
                           {"connected", device.connected},
                           {"rumble", device.rumble}});
    json = {{"available", value.available},
            {"revision", value.revision},
            {"active", value.active},
            {"devices", std::move(devices)}};
}
void from_json(const Json& json, GamepadTopology& value) {
    gamepad_record(json, {"available", "revision", "active", "devices"});
    if (!json.at("available").is_boolean() || !json.at("active").is_string() ||
        json.at("active").get_ref<const std::string&>().size() > 128 ||
        !json.at("devices").is_array() || json.at("devices").size() > gamepad_device_limit)
        throw std::runtime_error("Invalid gamepad topology record");
    GamepadTopology candidate;
    candidate.available = json.at("available").get<bool>();
    candidate.revision = gamepad_unsigned(json.at("revision"));
    candidate.active = json.at("active").get<std::string>();
    for (const auto& row : json.at("devices")) {
        gamepad_record(row, {"device", "name", "slot", "connected", "rumble"});
        if (!row.at("device").is_string() || !row.at("name").is_string() ||
            row.at("device").get_ref<const std::string&>().size() > 128 ||
            row.at("name").get_ref<const std::string&>().size() > 256 ||
            !row.at("connected").is_boolean() || !row.at("rumble").is_boolean())
            throw std::runtime_error("Invalid gamepad device record");
        candidate.devices.push_back(
            {row.at("device").get<std::string>(), row.at("name").get<std::string>(),
             gamepad_slot_value(row.at("slot")), row.at("connected").get<bool>(),
             row.at("rumble").get<bool>()});
    }
    candidate.validate();
    value = std::move(candidate);
}
std::string_view gamepad_sensor_name(GamepadSensorKind kind) {
    switch (kind) {
    case GamepadSensorKind::Accelerometer:
        return "accelerometer";
    case GamepadSensorKind::Gyroscope:
        return "gyroscope";
    }
    throw std::runtime_error("Unsupported gamepad sensor kind");
}
void GamepadSensorConfig::validate() const {
    if (!revision || !gamepad_token(device))
        throw std::runtime_error("Sensor configuration needs a current device/revision");
    (void)gamepad_sensor_name(sensor);
}
void to_json(Json& json, const GamepadSensorConfig& value) {
    value.validate();
    json = {{"revision", value.revision},
            {"device", value.device},
            {"sensor", gamepad_sensor_name(value.sensor)},
            {"enabled", value.enabled}};
}
void from_json(const Json& json, GamepadSensorConfig& value) {
    gamepad_record(json, {"revision", "device", "sensor", "enabled"}, {"operation"});
    if (json.contains("operation") && json.at("operation") != "gamepad_sensor")
        throw std::runtime_error("Unsupported gamepad sensor operation");
    if (!json.at("device").is_string() ||
        json.at("device").get_ref<const std::string&>().size() > 128 ||
        !json.at("sensor").is_string() || !json.at("enabled").is_boolean())
        throw std::runtime_error(
            "Sensor configuration needs a device, sensor kind and enabled flag");
    GamepadSensorKind kind;
    if (json.at("sensor") == "accelerometer")
        kind = GamepadSensorKind::Accelerometer;
    else if (json.at("sensor") == "gyroscope")
        kind = GamepadSensorKind::Gyroscope;
    else
        throw std::runtime_error("Gamepad sensor must be accelerometer or gyroscope");
    GamepadSensorConfig candidate{gamepad_unsigned(json.at("revision")),
                                  json.at("device").get<std::string>(), kind,
                                  json.at("enabled").get<bool>()};
    candidate.validate();
    value = std::move(candidate);
}
void GamepadSensorSample::validate(bool routed, bool connected) const {
    if (!std::isfinite(rate_hz) || rate_hz < 0 || (!supported && (enabled || rate_hz != 0)) ||
        (enabled && !connected) ||
        std::any_of(value.begin(), value.end(), [](double axis) { return !std::isfinite(axis); }))
        throw std::runtime_error("Invalid gamepad sensor support, mode, rate or SI values");
    if (measured) {
        if (!supported || !enabled || !routed || !connected || !timestamp_ns)
            throw std::runtime_error("Gamepad motion requires a fresh routed enabled sample");
    } else if (timestamp_ns || sensor_timestamp_ns || value != std::array<double, 3>{})
        throw std::runtime_error("Absent gamepad measurements must not carry stale data");
}
void GamepadMotion::validate() const {
    if (devices.size() > gamepad_device_limit || (!devices.empty() && !revision) ||
        (!available && (routed || revision || !active.empty() || !devices.empty())))
        throw std::runtime_error("Invalid gamepad motion host observation");
    std::set<std::string> tokens;
    std::set<std::uint32_t> slots;
    for (const auto& device : devices) {
        if (!gamepad_token(device.device) || !tokens.insert(device.device).second ||
            device.slot > gamepad_slot_limit || (device.slot && !slots.insert(device.slot).second))
            throw std::runtime_error("Invalid gamepad motion connection or slot");
        device.accelerometer.validate(routed, device.connected);
        device.gyroscope.validate(routed, device.connected);
    }
    if (!active.empty() && !tokens.contains(active))
        throw std::runtime_error("Active gamepad is absent from the motion observation");
}
void GamepadMotion::validate(const GamepadTopology& topology) const {
    validate();
    topology.validate();
    if (available != topology.available || revision != topology.revision ||
        active != topology.active || devices.size() != topology.devices.size())
        throw std::runtime_error(
            "Gamepad motion does not match the current connection observation");
    for (const auto& device : devices) {
        const auto found =
            std::find_if(topology.devices.begin(), topology.devices.end(),
                         [&](const auto& row) { return row.device == device.device; });
        if (found == topology.devices.end() || found->slot != device.slot ||
            (device.connected && !found->connected))
            throw std::runtime_error("Gamepad motion contains a stale connection or slot");
        // Native disconnect can occur between the two copied observations.
        // A fresher disconnected/empty sample is safe; it cannot resurrect a
        // connection already observed disconnected by the topology owner.
    }
}
void to_json(Json& json, const GamepadMotion& value) {
    value.validate();
    Json devices = Json::array();
    for (const auto& device : value.devices)
        devices.push_back({{"device", device.device},
                           {"slot", device.slot},
                           {"connected", device.connected},
                           {"accelerometer", sensor_sample_json(device.accelerometer)},
                           {"gyroscope", sensor_sample_json(device.gyroscope)}});
    json = {{"available", value.available},
            {"routed", value.routed},
            {"revision", value.revision},
            {"active", value.active},
            {"devices", std::move(devices)}};
}
void from_json(const Json& json, GamepadMotion& value) {
    gamepad_record(json, {"available", "routed", "revision", "active", "devices"});
    if (!json.at("available").is_boolean() || !json.at("routed").is_boolean() ||
        !json.at("active").is_string() ||
        json.at("active").get_ref<const std::string&>().size() > 128 ||
        !json.at("devices").is_array() || json.at("devices").size() > gamepad_device_limit)
        throw std::runtime_error("Invalid gamepad motion record");
    GamepadMotion candidate;
    candidate.available = json.at("available").get<bool>();
    candidate.routed = json.at("routed").get<bool>();
    candidate.revision = gamepad_unsigned(json.at("revision"));
    candidate.active = json.at("active").get<std::string>();
    for (const auto& row : json.at("devices")) {
        gamepad_record(row, {"device", "slot", "connected", "accelerometer", "gyroscope"});
        if (!row.at("device").is_string() ||
            row.at("device").get_ref<const std::string&>().size() > 128 ||
            !row.at("connected").is_boolean())
            throw std::runtime_error("Invalid gamepad motion device record");
        candidate.devices.push_back(
            {row.at("device").get<std::string>(), gamepad_slot_value(row.at("slot")),
             row.at("connected").get<bool>(), read_sensor_sample(row.at("accelerometer")),
             read_sensor_sample(row.at("gyroscope"))});
    }
    candidate.validate();
    value = std::move(candidate);
}
void GamepadRumble::validate() const {
    if (!std::isfinite(low) || !std::isfinite(high) || low < 0 || low > 1 || high < 0 || high > 1 ||
        duration_ms > 5000 || (duration_ms == 0 && (low != 0 || high != 0)) ||
        slot > gamepad_slot_limit)
        throw std::runtime_error("Gamepad rumble requires finite strengths in [0,1] and 1..5000 "
                                 "ms; slots are 1..4 or 0 for active; zero strengths stop");
}
void to_json(nlohmann::json& json, const GamepadRumble& value) {
    value.validate();
    json = {{"low", value.low}, {"high", value.high}, {"duration_ms", value.duration_ms}};
    if (value.slot)
        json["slot"] = value.slot;
}
void from_json(const nlohmann::json& json, GamepadRumble& value) {
    if (!json.is_object() || !json.at("low").is_number() || !json.at("high").is_number() ||
        !json.at("duration_ms").is_number_integer() || json.at("duration_ms") < 0 ||
        json.at("duration_ms") > 5000)
        throw std::runtime_error(
            "Gamepad rumble requires numeric low/high and integer duration_ms in [0,5000]");
    GamepadRumble candidate{json.at("low").get<double>(), json.at("high").get<double>(),
                            json.at("duration_ms").get<std::uint32_t>(),
                            json.contains("slot") ? gamepad_slot_value(json.at("slot")) : 0};
    candidate.validate();
    value = candidate;
}
namespace {
std::string radial_partner(const InputBinding& binding) {
    auto partner = binding.control;
    partner.back() = partner.back() == 'x' ? 'y' : 'x';
    return partner;
}
bool binding_reads(const InputBinding& binding, const std::string& control) {
    return binding.control == control || (binding.radial && radial_partner(binding) == control);
}
void validate_response(double deadzone, double outer, double exponent) {
    if (!std::isfinite(deadzone) || deadzone < 0 || deadzone >= 1 || !std::isfinite(outer) ||
        outer < 0 || outer >= 1 - deadzone || !std::isfinite(exponent) || exponent < .1 ||
        exponent > 8)
        throw std::runtime_error("Input requires nonnegative inner/outer deadzones with positive "
                                 "remaining travel and response exponent 0.1..8");
}
double response_magnitude(double magnitude, double deadzone, double outer, double exponent) {
    const auto mapped = std::clamp((magnitude - deadzone) / (1 - deadzone - outer), 0.0, 1.0);
    return exponent == 1 ? mapped : std::pow(mapped, exponent);
}
} // namespace

double input_axis(double value, double deadzone, double outer, double exponent) {
    validate_response(deadzone, outer, exponent);
    if (!std::isfinite(value) || std::abs(value) > 1)
        throw std::runtime_error("Invalid analog axis sample");
    return std::copysign(response_magnitude(std::abs(value), deadzone, outer, exponent), value);
}
InputVector input_stick(double x, double y, double deadzone, double outer, double exponent) {
    validate_response(deadzone, outer, exponent);
    if (!std::isfinite(x) || !std::isfinite(y) || std::abs(x) > 1 || std::abs(y) > 1)
        throw std::runtime_error("Invalid stick sample");
    const auto length = std::hypot(x, y);
    if (length <= deadzone || length == 0)
        return {};
    const auto scale =
        outer == 0 && exponent == 1
            ? (std::min(length, 1.0) - deadzone) / ((1 - deadzone) * length)
            : response_magnitude(std::min(length, 1.0), deadzone, outer, exponent) / length;
    return {x * scale, y * scale};
}
const std::vector<InputControl>& input_controls() {
    static const auto controls = [] {
        std::vector<InputControl> result;
        for (char key = 'a'; key <= 'z'; ++key)
            result.push_back({std::string("key.") + key, false, true});
        for (char key = '0'; key <= '9'; ++key)
            result.push_back({std::string("key.") + key, false, true});
        for (const auto* key : {"space", "left", "right", "up", "down", "lshift", "rshift", "lctrl",
                                "rctrl", "lalt", "ralt", "tab", "enter", "backspace", "escape"})
            result.push_back({std::string("key.") + key, false, true});
        for (const auto* button : {"left", "right", "middle", "x1", "x2"})
            result.push_back({std::string("mouse.") + button, false, true});
        for (const auto* axis : {"delta_x", "delta_y", "wheel_x", "wheel_y"})
            result.push_back({std::string("mouse.") + axis, true, false});
        for (const auto* button :
             {"south", "east", "west", "north", "back", "guide", "start", "left_stick",
              "right_stick", "left_shoulder", "right_shoulder", "up", "down", "left", "right"})
            result.push_back({std::string("pad.") + button, false, true});
        for (const auto* axis :
             {"left_x", "left_y", "right_x", "right_y", "left_trigger", "right_trigger"})
            result.push_back({std::string("pad.") + axis, false, false,
                              std::string_view(axis).ends_with("_trigger")});
        for (std::size_t contact = 0; contact < touch_contact_limit; ++contact) {
            const auto prefix = "touch." + std::to_string(contact) + ".";
            result.push_back({prefix + "down", false, true});
            for (const auto* axis : {"x", "y", "pressure"})
                result.push_back({prefix + axis, false, false, true});
        }
        result.push_back({"touch.gesture.active", false, true});
        for (const auto* axis : {"pan_x", "pan_y", "pinch", "rotation"})
            result.push_back({std::string("touch.gesture.") + axis, true, false});
        for (const auto* pulse : {"tap", "double_tap", "long_press", "swipe_left", "swipe_right",
                                  "swipe_up", "swipe_down"})
            result.push_back({std::string("touch.gesture.") + pulse, false, true});
        // Derive indexed controls from the existing registry's gamepad entries.
        // Append them so existing discovery order and IDs stay stable.
        std::vector<InputControl> gamepad;
        for (const auto& control : result)
            if (control.id.starts_with("pad."))
                gamepad.push_back(control);
        for (std::size_t slot = 1; slot <= gamepad_slot_limit; ++slot)
            for (auto control : gamepad) {
                control.id = "pad." + std::to_string(slot) + "." + control.id.substr(4);
                result.push_back(std::move(control));
            }
        return result;
    }();
    return controls;
}
const InputControl& input_control(const std::string& id) {
    static const auto lookup = [] {
        std::map<std::string, const InputControl*> result;
        for (const auto& control : input_controls())
            result.emplace(control.id, &control);
        return result;
    }();
    const auto found = lookup.find(id);
    if (found != lookup.end())
        return *found->second;
    throw std::runtime_error("Unsupported input control: " + id);
}
std::string gamepad_control_id(std::uint32_t slot, std::string_view leaf) {
    if (slot > gamepad_slot_limit)
        throw std::runtime_error("Gamepad slot must be 1..4 or 0 for active controls");
    const auto id = (slot ? "pad." + std::to_string(slot) + "." : "pad.") + std::string(leaf);
    return input_control(id).id;
}
std::optional<std::uint32_t> gamepad_control_slot(const std::string& id) {
    (void)input_control(id);
    if (!id.starts_with("pad."))
        return {};
    const auto dot = id.find('.', 4);
    return dot == std::string::npos ? 0 : std::uint32_t(id[4] - '0');
}
bool is_gamepad_stick_control(const std::string& id) {
    const auto& control = input_control(id);
    if (!id.starts_with("pad.") || control.digital || control.relative)
        return false;
    const auto axis = std::string_view(id).substr(id.rfind('.') + 1);
    return axis == "left_x" || axis == "left_y" || axis == "right_x" || axis == "right_y";
}
InputMap::InputMap(Json source) : source_(std::move(source)) {
    if (!source_.is_object() || (source_.at("version") != 1 && source_.at("version") != 2) ||
        !source_.at("version").is_number_integer() || !source_.at("actions").is_array() ||
        source_.at("actions").size() > 64)
        throw std::runtime_error("Input map requires version 1 or 2 and at most 64 actions");
    std::set<std::string> contexts;
    if (source_.at("version") == 2) {
        const auto& declarations = source_.at("contexts");
        if (!declarations.is_array() || declarations.empty() || declarations.size() > 32)
            throw std::runtime_error("Input map requires 1 to 32 contexts");
        for (const auto& item : declarations) {
            InputContext context;
            context.name = item.at("name").get<std::string>();
            if (context.name.empty() || context.name.size() > 128 ||
                context.name.find('\0') != std::string::npos ||
                !contexts.insert(context.name).second)
                throw std::runtime_error("Invalid/duplicate input context name");
            const auto priority = item.value("priority", Json(0));
            if (!priority.is_number_integer() || priority.get<double>() < -10000 ||
                priority.get<double>() > 10000)
                throw std::runtime_error("Invalid input context priority");
            context.priority = priority.get<int>();
            context.consume = item.value("consume", true);
            context.active = item.value("active", false);
            const auto phase = item.value("phase", std::string("fixed"));
            if (phase != "fixed" && phase != "control")
                throw std::runtime_error("Input context phase must be fixed or control");
            context.control = phase == "control";
            contexts_.push_back(std::move(context));
        }
    } else if (source_.contains("contexts")) {
        throw std::runtime_error("Input contexts require input map version 2");
    }
    std::set<ActionId> ids;
    for (const auto& a : source_.at("actions")) {
        InputAction action;
        action.id = a.at("id").get<ActionId>();
        action.name = a.at("name").get<std::string>();
        if (!contexts_.empty()) {
            action.context = a.at("context").get<std::string>();
            if (!contexts.contains(action.context))
                throw std::runtime_error("Input action references an unknown context");
        } else if (a.contains("context")) {
            throw std::runtime_error("Input action context requires input map version 2");
        }
        if (!ids.insert(action.id).second || action.name.empty() || action.name.size() > 128)
            throw std::runtime_error("Invalid/duplicate input action identity or name");
        const auto kind = a.at("kind").get<std::string>();
        if (kind == "digital")
            action.kind = ActionKind::Digital;
        else if (kind == "axis1")
            action.kind = ActionKind::Axis1;
        else if (kind == "axis2")
            action.kind = ActionKind::Axis2;
        else
            throw std::runtime_error("Unknown input action kind");
        if (!a.at("bindings").is_array() || a.at("bindings").size() > 16)
            throw std::runtime_error("An action supports at most 16 bindings");
        for (const auto& b : a.at("bindings")) {
            InputBinding binding{b.at("control").get<std::string>(), b.value("x", 1.0),
                                 b.value("y", 0.0), b.value("deadzone", 0.0),
                                 b.value("radial", false)};
            const auto& control = input_control(binding.control);
            for (const auto* field : {"outer_deadzone", "response_exponent"})
                if (b.contains(field) && !b.at(field).is_number())
                    throw std::runtime_error("Input binding response values must be numeric");
            binding.outer_deadzone = b.value("outer_deadzone", 0.0);
            binding.response_exponent = b.value("response_exponent", 1.0);
            validate_response(binding.deadzone, binding.outer_deadzone, binding.response_exponent);
            binding.threshold = b.value("threshold", .5);
            const auto direction = b.value("direction", Json(1));
            if (!direction.is_number_integer() || (direction != 1 && direction != -1))
                throw std::runtime_error("Input binding direction must be +1 or -1");
            binding.direction = direction.get<int>();
            if (!std::isfinite(binding.x) || !std::isfinite(binding.y) ||
                std::abs(binding.x) > 100 || std::abs(binding.y) > 100 ||
                (action.kind != ActionKind::Axis2 && binding.y != 0) ||
                (action.kind == ActionKind::Digital &&
                 (binding.x != 1 || binding.y != 0 ||
                  binding.control.starts_with("mouse.delta_"))) ||
                !std::isfinite(binding.threshold) || binding.threshold <= 0 ||
                binding.threshold > 1 ||
                (control.digital && (binding.direction != 1 || binding.threshold != .5)) ||
                (action.kind != ActionKind::Digital &&
                 (binding.direction != 1 || binding.threshold != .5)) ||
                (control.unipolar && binding.direction != 1) ||
                ((control.relative || control.digital) &&
                 (binding.deadzone != 0 || binding.outer_deadzone != 0 ||
                  binding.response_exponent != 1)))
                throw std::runtime_error("Invalid input binding scale/deadzone or digital control");
            if (binding.radial && !is_gamepad_stick_control(binding.control))
                throw std::runtime_error("Radial dead zone requires a gamepad stick axis");
            if (source_.at("version") == 2 &&
                std::any_of(action.bindings.begin(), action.bindings.end(),
                            [&](const auto& old) { return old.control == binding.control; }))
                throw std::runtime_error("Duplicate control on the same input action");
            action.bindings.push_back(std::move(binding));
        }
        actions_.push_back(std::move(action));
    }
}
InputMap InputMap::with_bindings(ActionId id, const Json& bindings) const {
    std::set<std::string> controls;
    for (const auto& binding : bindings)
        if (!controls.insert(binding.at("control").get<std::string>()).second)
            throw std::runtime_error("Duplicate control on the same input action");
    auto candidate = source_;
    for (auto& action : candidate["actions"])
        if (action.at("id").get<ActionId>() == id) {
            action["bindings"] = bindings;
            return InputMap(std::move(candidate));
        }
    throw std::runtime_error("Binding targets an unknown ActionId");
}
std::vector<InputBindingConflict> InputMap::binding_conflicts(ActionId id,
                                                              const std::string& control) const {
    (void)input_control(control);
    const auto target = std::find_if(actions_.begin(), actions_.end(),
                                     [&](const auto& action) { return action.id == id; });
    if (target == actions_.end())
        throw std::runtime_error("Binding targets an unknown ActionId");
    std::vector<InputBindingConflict> result;
    for (const auto& action : actions_)
        for (const auto& binding : action.bindings)
            if (binding_reads(binding, control))
                result.push_back(
                    {action.id, action.context, control, action.context == target->context});
    return result;
}
void validate_input_event(const InputEvent& event) {
    if (event.reset)
        return;
    const auto& control = input_control(event.control);
    if (!std::isfinite(event.value) || std::abs(event.value) > (control.relative ? 1000000 : 1) ||
        (control.digital && event.value != 0 && event.value != 1) ||
        (control.unipolar && event.value < 0))
        throw std::runtime_error("Invalid input control value");
}
void to_json(Json& j, const InputEvent& e) {
    j = e.reset ? Json{{"reset", true}} : Json{{"control", e.control}, {"value", e.value}};
}
void from_json(const Json& j, InputEvent& e) {
    e.reset = j.value("reset", false);
    if (e.reset) {
        e.control.clear();
        e.value = 0;
        return;
    }
    e.control = j.at("control").get<std::string>();
    e.value = j.at("value").get<double>();
}
void RuntimeInput::configure(InputMap map) {
    map_ = std::move(map);
    controls_.clear();
    pending_.clear();
    current_ = {};
    control_current_ = {};
    control_deltas_.clear();
    listening_ = false;
    rebind_held_.clear();
    rebind_result_.reset();
    active_contexts_.clear();
    for (const auto& context : map_.contexts())
        if (context.active)
            active_contexts_.insert(context.name);
    rebuild_routes();
}
void RuntimeInput::replace_map(InputMap map) {
    std::vector<std::string> active;
    for (const auto& context : map.contexts())
        if (active_contexts_.contains(context.name))
            active.push_back(context.name);
    configure(std::move(map));
    activate_contexts(active);
}
bool RuntimeInput::control_action(const InputAction& action) const {
    for (const auto& context : map_.contexts())
        if (context.name == action.context)
            return context.control;
    return false;
}
void RuntimeInput::begin_rebind(std::uint32_t gamepad_slot) {
    if (gamepad_slot > gamepad_slot_limit)
        throw std::runtime_error("Rebinding gamepad slot must be 0..4");
    rebind_held_.clear();
    for (const auto& [control, value] : controls_)
        if (!input_control(control).relative && std::abs(value) > .25)
            rebind_held_.insert(control);
    release_all();
    rebind_result_.reset();
    rebind_gamepad_slot_ = gamepad_slot;
    listening_ = true;
}
void RuntimeInput::cancel_rebind() {
    listening_ = false;
    rebind_held_.clear();
    rebind_result_.reset();
    release_all();
}
std::optional<InputEvent> RuntimeInput::take_rebind() {
    auto result = std::move(rebind_result_);
    rebind_result_.reset();
    return result;
}
bool RuntimeInput::context_active(const std::string& name) const {
    return active_contexts_.contains(name);
}
void RuntimeInput::activate_contexts(const std::vector<std::string>& names) {
    std::set<std::string> candidate;
    for (const auto& name : names) {
        if (!candidate.insert(name).second ||
            std::none_of(map_.contexts().begin(), map_.contexts().end(),
                         [&](const auto& context) { return context.name == name; }))
            throw std::runtime_error("Invalid/duplicate active input context: " + name);
    }
    if (candidate == active_contexts_)
        return;
    release_all();
    active_contexts_ = std::move(candidate);
    rebuild_routes();
}
void RuntimeInput::rebuild_routes() {
    routes_.clear();
    if (map_.contexts().empty()) {
        for (const auto& action : map_.actions())
            for (const auto& binding : action.bindings) {
                routes_[action.id].insert(binding.control);
                if (binding.radial)
                    routes_[action.id].insert(radial_partner(binding));
            }
        return;
    }
    std::vector<const InputContext*> contexts;
    for (const auto& context : map_.contexts())
        if (active_contexts_.contains(context.name))
            contexts.push_back(&context);
    std::stable_sort(contexts.begin(), contexts.end(),
                     [](auto a, auto b) { return a->priority > b->priority; });
    std::set<std::string> consumed;
    for (const auto* context : contexts) {
        std::set<std::string> claims;
        for (const auto& action : map_.actions())
            if (action.context == context->name)
                for (const auto& binding : action.bindings) {
                    if (!consumed.contains(binding.control))
                        routes_[action.id].insert(binding.control);
                    if (context->consume)
                        claims.insert(binding.control);
                    if (binding.radial) {
                        const auto partner = radial_partner(binding);
                        if (!consumed.contains(partner))
                            routes_[action.id].insert(partner);
                        if (context->consume)
                            claims.insert(partner);
                    }
                }
        consumed.insert(claims.begin(), claims.end());
    }
}
ActionState RuntimeInput::evaluate(const InputAction& action, bool relative) const {
    ActionState state;
    const auto route = routes_.find(action.id);
    if (route == routes_.end())
        return state;
    for (const auto& b : action.bindings) {
        if (!route->second.contains(b.control))
            continue;
        const auto& c = input_control(b.control);
        if (c.relative != relative)
            continue;
        const auto& values = relative && control_action(action) ? control_deltas_ : controls_;
        const auto found = values.find(b.control);
        double value = found == values.end() ? 0 : found->second;
        if (b.radial) {
            const auto prefix = b.control.substr(0, b.control.size() - 1);
            auto sample = [&](const std::string& axis) {
                // A higher context may consume the other axis. Never read it
                // through radial processing and bypass that routing decision.
                const auto position = controls_.find(axis);
                return route->second.contains(axis) && position != controls_.end()
                           ? position->second
                           : 0.0;
            };
            const auto stick = input_stick(sample(prefix + "x"), sample(prefix + "y"), b.deadzone,
                                           b.outer_deadzone, b.response_exponent);
            value = b.control.back() == 'x' ? stick.x : stick.y;
        } else if (!c.relative && !c.digital)
            value = input_axis(value, b.deadzone, b.outer_deadzone, b.response_exponent);
        if (action.kind == ActionKind::Digital)
            state.held |= c.digital ? value != 0 : value * b.direction >= b.threshold;
        else {
            state.x += value * b.x;
            state.y += value * b.y;
        }
    }
    if (action.kind == ActionKind::Digital)
        state.x = state.held ? 1 : 0;
    else if (!relative) {
        state.x = std::clamp(state.x, -1.0, 1.0);
        state.y = std::clamp(state.y, -1.0, 1.0);
        if (action.kind == ActionKind::Axis2) {
            const auto length = std::hypot(state.x, state.y);
            if (length > 1) {
                state.x /= length;
                state.y /= length;
            }
        }
    }
    return state;
}
void RuntimeInput::event(const InputEvent& e) {
    if (e.reset) {
        release_all();
        return;
    }
    if (input_control(e.control).relative) {
        controls_[e.control] = std::clamp(controls_[e.control] + e.value, -1000000.0, 1000000.0);
        control_deltas_[e.control] =
            std::clamp(control_deltas_[e.control] + e.value, -1000000.0, 1000000.0);
    } else
        controls_[e.control] = e.value;
    for (const auto& a : map_.actions())
        if (a.kind == ActionKind::Digital &&
            std::any_of(a.bindings.begin(), a.bindings.end(),
                        [&](const auto& binding) { return binding_reads(binding, e.control); })) {
            auto& previous = pending_[a.id];
            const auto next = evaluate(a, false);
            previous.pressed |= next.held && !previous.held;
            previous.released |= !next.held && previous.held;
            previous.held = next.held;
            previous.x = next.x;
            if (input_control(e.control).relative && !previous.held) {
                const auto route = routes_.find(a.id);
                if (route != routes_.end() && route->second.contains(e.control))
                    for (const auto& binding : a.bindings)
                        if (binding.control == e.control) {
                            const auto& values = control_action(a) ? control_deltas_ : controls_;
                            if (values.at(e.control) * binding.direction >= binding.threshold)
                                previous.pressed = previous.released = true;
                        }
            }
        }
}
void RuntimeInput::submit(const std::vector<InputEvent>& events) {
    if (events.size() > 4096)
        throw std::runtime_error("Input batch exceeds 4096 events");
    for (const auto& event : events)
        validate_input_event(event);
    if (listening_) {
        for (const auto& e : events) {
            if (e.reset || (e.control == "key.escape" && e.value != 0)) {
                cancel_rebind();
                break;
            }
            if (const auto slot = gamepad_control_slot(e.control);
                slot && *slot != rebind_gamepad_slot_)
                continue;
            if (rebind_held_.contains(e.control)) {
                if (std::abs(e.value) <= .25)
                    rebind_held_.erase(e.control);
                continue;
            }
            // Incidental pointer motion never steals a button/key rebind.
            if (e.control == "mouse.delta_x" || e.control == "mouse.delta_y" ||
                (e.control.starts_with("touch.") && !input_control(e.control).digital))
                continue;
            if (std::abs(e.value) >= .5) {
                rebind_result_ = e;
                listening_ = false;
                rebind_held_.clear();
                break;
            }
        }
        return;
    }
    for (const auto& e : events)
        event(e);
}
void RuntimeInput::release_all() {
    controls_.clear();
    control_deltas_.clear();
    for (const auto& a : map_.actions()) {
        auto& p = pending_[a.id];
        const auto& snapshot = control_action(a) ? control_current_ : current_;
        const auto current = snapshot.actions.find(a.id);
        const bool was_held = p.held || (current != snapshot.actions.end() && current->second.held);
        p = {};
        p.released = was_held;
    }
}
const InputSnapshot& RuntimeInput::latch(std::uint64_t tick) { return latch_domain(tick, false); }
const InputSnapshot& RuntimeInput::latch_controls(std::uint64_t frame) {
    return latch_domain(frame, true);
}
const InputSnapshot& RuntimeInput::latch_domain(std::uint64_t sequence, bool control) {
    auto& snapshot = control ? control_current_ : current_;
    snapshot.tick = sequence;
    snapshot.actions.clear();
    for (const auto& a : map_.actions()) {
        if (control_action(a) != control)
            continue;
        auto state = evaluate(a, false);
        if (a.kind == ActionKind::Digital) {
            auto& p = pending_[a.id];
            state.pressed = p.pressed;
            state.released = p.released;
            p.pressed = p.released = false;
        } else {
            const auto delta = evaluate(a, true);
            state.x += delta.x;
            state.y += delta.y;
        }
        snapshot.actions.emplace(a.id, state);
    }
    auto& values = control ? control_deltas_ : controls_;
    for (auto& [name, value] : values)
        if (input_control(name).relative)
            value = 0;
    return snapshot;
}
void InputMonitor::consume(const InputSnapshot& snapshot) {
    tick_ = snapshot.tick;
    std::erase_if(counts_,
                  [&](const auto& entry) { return !snapshot.actions.contains(entry.first); });
    for (const auto& [id, state] : snapshot.actions) {
        auto& c = counts_[id];
        c.state = state;
        if (state.pressed) {
            ++c.presses;
            c.pressed_tick = tick_;
        }
        if (state.released) {
            ++c.releases;
            c.released_tick = tick_;
        }
    }
}
Json InputMonitor::status(const InputMap& map) const {
    Json actions = Json::array();
    for (const auto& a : map.actions()) {
        const auto it = counts_.find(a.id);
        const auto c = it == counts_.end() ? Counts{} : it->second;
        actions.push_back({{"id", a.id},
                           {"name", a.name},
                           {"x", c.state.x},
                           {"y", c.state.y},
                           {"held", c.state.held},
                           {"pressed", c.state.pressed},
                           {"released", c.state.released},
                           {"presses", c.presses},
                           {"releases", c.releases},
                           {"pressed_tick", c.pressed_tick},
                           {"released_tick", c.released_tick}});
    }
    return {{"tick", tick_}, {"actions", actions}};
}
} // namespace forge
