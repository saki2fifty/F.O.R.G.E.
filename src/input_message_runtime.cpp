#include "input_message_runtime.hpp"
#include "authored_component.hpp"
#include "builtins.hpp"
#include <algorithm>
#include <forge/input_message.hpp>
#include <limits>
#include <memory>

namespace forge::detail {
namespace {
constexpr std::size_t message_limit = 256, value_limit = 65536, tick_value_limit = 4 * 1024 * 1024;
struct NativeValue {
    ecs_world_t* world;
    ecs_entity_t type;
    void operator()(void* value) const { ecs_value_free(world, type, value); }
};
struct Pending {
    flecs::entity_t source, type;
    std::size_t row;
    std::unique_ptr<void, NativeValue> value;
};
} // namespace
void InputMessageRuntime::tick(const InputSnapshot& input, const InputMap& map) {
    auto& world = context_.world();
    if (!context_.on_owner_thread() || world.is_readonly())
        throw std::runtime_error("Input Messages require the immediate writable runtime owner");
    status_["tick"] = input.tick;
    status_["emitted"] = 0;
    status_["overflow"] = 0;
    status_["sources"] = Json::array();
    auto& rows = status_["sources"];
    for (auto& [id, observation] : observations_) {
        (void)id;
        observation.seen = false;
    }
    std::map<EntityId, flecs::entity_t> sources;
    world.each([&](flecs::entity entity, const InputMessage&) {
        if (entity.has<SceneMember>(scene_.membership()))
            if (const auto ref = context_.reference(entity))
                sources.emplace(ref->entity, entity);
    });
    if (sources.empty()) {
        observations_.clear();
        return;
    }
    std::vector<Pending> pending;
    std::size_t bytes = 0;
    ApplicationStateActivation activity(context_);
    for (const auto& [id, handle] : sources) {
        if (rows.size() == message_limit) {
            status_["overflow"] = sources.size() - rows.size();
            break;
        }
        auto entity = world.entity(handle);
        const auto config = entity.get<InputMessage>();
        rows.push_back({{"entity", EntityRef{scene_.asset_id(), id}},
                        {"action", config.action},
                        {"payload", config.payload},
                        {"trigger", config.trigger},
                        {"state", "Ready"}});
        auto& row = rows.back();
        try {
            config.validate();
            if (!config.enabled || !activity.active(entity)) {
                row["state"] = "Inactive";
                continue;
            }
            if (config.action.empty() || config.payload.empty()) {
                row["state"] = "Unassigned";
                continue;
            }
            const auto action_id = ActionId::parse(config.action);
            const auto definition =
                std::find_if(map.actions().begin(), map.actions().end(),
                             [&](const auto& value) { return value.id == action_id; });
            if (definition == map.actions().end())
                throw std::runtime_error("Action is absent from the active input map");
            if (definition->kind != ActionKind::Digital &&
                config.trigger != std::uint32_t(InputMessageTrigger::Changed))
                throw std::runtime_error("Input Message requires a digital action");
            for (const auto& context : map.contexts())
                if (context.name == definition->context && context.control)
                    throw std::runtime_error("Input Message cannot use a control-frame action");
            const auto action = input.actions.find(action_id);
            if (action == input.actions.end())
                throw std::runtime_error("Action is absent from the active input map");
            const auto& current = action->second;
            bool triggered = config.trigger == std::uint32_t(InputMessageTrigger::Released)
                                 ? current.released
                                 : current.pressed;
            if (config.trigger == std::uint32_t(InputMessageTrigger::Changed)) {
                auto& previous = observations_[id];
                if (previous.source != handle || previous.action != action_id ||
                    previous.kind != definition->kind || previous.context != definition->context ||
                    previous.payload != config.payload) {
                    previous.source = handle;
                    previous.action = action_id;
                    previous.kind = definition->kind;
                    previous.context = definition->context;
                    previous.payload = config.payload;
                    previous.value = {};
                }
                triggered =
                    definition->kind == ActionKind::Digital
                        ? current.pressed || current.released || current.held != previous.value.held
                        : current.x != previous.value.x || current.y != previous.value.y;
                previous.value = current;
                previous.seen = true;
            }
            ecs_entity_t type = 0;
            const Builtin* builtin_value = nullptr;
            const AuthoredCodec* authored_value = nullptr;
            for (const auto& builtin : builtins())
                if (config.payload == builtin.name) {
                    const auto path = "::" + config.payload;
                    type = ecs_lookup_path_w_sep(world, 0, path.c_str(), "::", "::", false);
                    builtin_value = &builtin;
                    break;
                }
            if (!type)
                for (const auto& codec : context_.authored_codecs())
                    if (config.payload == codec.key()) {
                        type = codec.native_type;
                        authored_value = &codec;
                        break;
                    }
            if (!type || !entity.has(type))
                throw std::runtime_error("Payload component is unavailable on this source");
            if (config.payload == "forge.input_message")
                throw std::runtime_error("Input Message cannot use itself as a payload");
            const auto* info = ecs_get_type_info(world, type);
            if (!info || info->size <= 0 || std::size_t(info->size) > value_limit ||
                (info->hooks.flags & (ECS_TYPE_HOOK_CTOR_ILLEGAL | ECS_TYPE_HOOK_COPY_ILLEGAL |
                                      ECS_TYPE_HOOK_DTOR_ILLEGAL)))
                throw std::runtime_error("Payload has no supported native value lifecycle");
            if (!triggered)
                continue;
            const auto value = builtin_value ? builtin_value->read(entity, true)
                                             : authored_value->read(entity, true);
            const auto size = value.dump().size() + std::size_t(info->size);
            if (size > value_limit)
                throw std::runtime_error("Input Message payload exceeds 64 KiB");
            if (size > tick_value_limit - bytes)
                throw std::runtime_error("Input Message tick payload budget exceeds 4 MiB");
            std::unique_ptr<void, NativeValue> copy(ecs_value_new(world, type), {world, type});
            if (!copy || ecs_value_copy(world, type, copy.get(), ecs_get_id(world, entity, type)))
                throw std::runtime_error("Input Message payload copy failed");
            bytes += size;
            pending.push_back({entity, type, rows.size() - 1, std::move(copy)});
        } catch (const std::exception& error) {
            row["state"] = "Error";
            row["diagnostic"] = error.what();
        }
    }
    std::erase_if(observations_, [](const auto& row) { return !row.second.seen; });
    // Capture every value before invoking project observers. The immediate native
    // system retains its outer deferral: structural observer writes merge at the
    // system boundary, after this captured batch and before Gameplay.
    for (auto& message : pending) {
        auto& row = rows.at(message.row);
        if (!world.entity(message.source).is_alive()) {
            row["state"] = "Source removed";
            continue;
        }
        ecs_event_desc_t event{};
        event.event = message.type;
        event.entity = message.source;
        event.const_param = message.value.get();
        // Default EcsAny event ID matches inherited sources without requiring an
        // owned payload column. The event type itself identifies the typed value.
        ecs_emit(world, &event);
        row["state"] = "Sent";
        status_["emitted"] = status_["emitted"].get<std::size_t>() + 1;
        const auto total = status_["emitted_total"].get<std::uint64_t>();
        if (total != std::numeric_limits<std::uint64_t>::max())
            status_["emitted_total"] = total + 1;
        status_["last_sent_tick"] = input.tick;
    }
}
} // namespace forge::detail
