#pragma once
#include <cstdio>
#include <forge/ecs_tools.hpp>
#include <iostream>
#ifdef _WIN32
#include <io.h>
#else
#include <unistd.h>
#endif
namespace forge {
// Pinned native JSON diagnostics include a direct stdout backtrace. Keep the
// protocol on a duplicate stream while all native stdout is routed to stderr.
class EcsProtocolOutput {
    FILE* stream_ = nullptr;

  public:
    EcsProtocolOutput() {
        std::fflush(stdout);
#ifdef _WIN32
        const int saved = _dup(_fileno(stdout));
        if (saved < 0)
            throw std::runtime_error("Cannot preserve protocol output");
        stream_ = _fdopen(saved, "w");
        if (!stream_) {
            _close(saved);
            throw std::runtime_error("Cannot open protocol output");
        }
        if (_dup2(_fileno(stderr), _fileno(stdout)) < 0) {
#else
        const int saved = dup(fileno(stdout));
        if (saved < 0)
            throw std::runtime_error("Cannot preserve protocol output");
        stream_ = fdopen(saved, "w");
        if (!stream_) {
            close(saved);
            throw std::runtime_error("Cannot open protocol output");
        }
        if (dup2(fileno(stderr), fileno(stdout)) < 0) {
#endif
            std::fclose(stream_);
            stream_ = nullptr;
            throw std::runtime_error("Cannot isolate native diagnostics");
        }
    }
    ~EcsProtocolOutput() {
        std::fflush(stdout);
#ifdef _WIN32
        _dup2(_fileno(stream_), _fileno(stdout));
#else
        dup2(fileno(stream_), fileno(stdout));
#endif
        std::fclose(stream_);
    }
    void send(const nlohmann::ordered_json& value) {
        const auto line = value.dump() + "\n";
        if (std::fwrite(line.data(), 1, line.size(), stream_) != line.size() ||
            std::fflush(stream_))
            throw std::runtime_error("ECS protocol output failed");
    }
};
inline int ecs_tools_stdio() {
    EcsProtocolOutput output;
    auto context = std::make_unique<EngineContext>(WorldRole::Preview);
    auto tools = std::make_unique<EcsTools>(context->world());
    while (std::cin) {
        std::string line;
        char c = 0;
        bool overflow = false;
        while (std::cin.get(c) && c != '\n') {
            if (line.size() < 1024 * 1024)
                line += c;
            else
                overflow = true;
        }
        if (line.empty() && !std::cin)
            break;
        nlohmann::ordered_json response;
        try {
            if (overflow)
                throw std::runtime_error("ECS tool request exceeds 1 MiB");
            const auto request = nlohmann::ordered_json::parse(
                line,
                [](int depth, nlohmann::ordered_json::parse_event_t, nlohmann::ordered_json&) {
                    if (depth > 64)
                        throw std::runtime_error("ECS tool request nesting exceeds 64");
                    return true;
                });
            const auto operation = request.at("operation").get<std::string>();
            nlohmann::ordered_json result;
            if (operation == "statistics")
                result = tools->statistics();
            else if (operation == "query")
                result = tools->query(request.at("expression"), request.value("offset", 0u),
                                      request.value("limit", 100u));
            else if (operation == "metrics")
                result = tools->metrics();
            else if (operation == "metric.create")
                result = {{"metric", tools->create_metric(request.at("source"), request.at("kind"),
                                                          request.value("member", false))}};
            else if (operation == "metric.remove") {
                tools->remove_metric(request.at("metric"));
                result = true;
            } else if (operation == "entity")
                result = tools->entity(request.at("entity"));
            else if (operation == "world.export")
                result = nlohmann::ordered_json::parse(tools->export_world());
            else if (operation == "world.import") {
                const auto source = request.at("document").dump();
                auto candidate = std::make_unique<EngineContext>(WorldRole::Preview);
                auto next_tools = std::make_unique<EcsTools>(candidate->world());
                ecs_from_json_desc_t desc{};
                desc.strict = true;
                if (!ecs_world_from_json(candidate->world().world(), source.c_str(), &desc))
                    throw std::runtime_error(
                        "Native world JSON import rejected; previous inspection world retained");
                tools.reset();
                context = std::move(candidate);
                tools = std::move(next_tools);
                result = {{"world", "isolated Preview"}, {"rest", "stopped"}};
            } else if (operation == "rest.start") {
                tools->start_rest(request.at("port"));
                result = {{"port", tools->rest_port()}};
            } else if (operation == "rest.stop") {
                tools->stop_rest();
                result = {{"port", 0}};
            } else if (operation == "rest.request")
                result = tools->rest_request(request.at("method"), request.at("path"));
            else if (operation == "poll") {
                tools->poll_rest(.01f);
                tools->sample(.5f);
                result = tools->alerts();
            } else
                throw std::runtime_error("Unknown ECS tool operation");
            response = {{"ok", true}, {"result", result}};
        } catch (const std::exception& e) {
            response = {{"ok", false}, {"error", e.what()}};
        }
        output.send(response);
    }
    return 0;
}
} // namespace forge
