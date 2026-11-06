#include "paint_texture_task.hpp"
#include <chrono>

namespace forge {
void PaintTextureTask::start(PaintTextureValue value, std::vector<PaintPoint> points,
                             PaintBrush brush, std::uint64_t session, std::uint64_t revision) {
    validate_paint_brush(brush);
    if (points.empty() || points.size() > paint_texture_stroke_point_limit)
        throw std::runtime_error("Paint stroke needs 1 to 256 points");
    launch([value = std::move(value), points = std::move(points),
            brush](std::stop_token stop) { return value.paint(points, brush, stop); },
           "Paint texture stroke", session, revision);
}
void PaintTextureTask::copy_pixels(PaintTextureValue value,
                                   std::function<Rgba8Pixels(std::stop_token)> load,
                                   std::uint64_t session, std::uint64_t revision) {
    if (!load)
        throw std::runtime_error("Pixel copy needs a selected pixel reader");
    launch([value = std::move(value), load = std::move(load)](
               std::stop_token stop) { return value.replace_pixels(load(stop), stop); },
           "Copy published image pixels", session, revision);
}
void PaintTextureTask::mapped_stroke(
    PaintTextureValue value, std::function<std::vector<PaintPoint>(std::stop_token)> project,
    PaintBrush brush, std::uint64_t session, std::uint64_t revision) {
    validate_paint_brush(brush);
    if (!project)
        throw std::runtime_error("Surface stroke needs selected mesh coordinates");
    launch([value = std::move(value), project = std::move(project),
            brush](std::stop_token stop) { return value.paint(project(stop), brush, stop); },
           "Paint mesh texture stroke", session, revision);
}
void PaintTextureTask::launch(std::function<PaintTextureValue(std::stop_token)> work,
                              std::string label, std::uint64_t session, std::uint64_t revision) {
    if (pending() || !session || !revision)
        throw std::runtime_error(
            "Pixel preparation needs an idle task and valid document revision");
    stop_ = std::stop_source{};
    future_ =
        std::async(std::launch::async, [work = std::move(work), label = std::move(label), session,
                                        revision, stop = stop_.get_token()]() mutable {
            Result result{session, revision, {}, {}, std::move(label)};
            try {
                result.value = work(stop);
            } catch (const std::exception& error) {
                result.error = error.what();
            }
            return result;
        });
}
std::optional<PaintTextureTask::Result> PaintTextureTask::poll() {
    if (!future_.valid() || future_.wait_for(std::chrono::seconds(0)) != std::future_status::ready)
        return {};
    return future_.get();
}
void PaintTextureTask::cancel() {
    stop_.request_stop();
    if (future_.valid())
        (void)future_.get();
}
} // namespace forge
