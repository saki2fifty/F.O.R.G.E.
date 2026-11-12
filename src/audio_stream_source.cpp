#include "audio_stream_source.hpp"
#include "native_io_path.hpp"
#include <forge/project_paths.hpp>
#include <stdexcept>

namespace forge::asset_detail {
namespace {
void checked(ma_result result, const char* context) {
    if (result != MA_SUCCESS)
        throw std::runtime_error(std::string(context) + ": " + ma_result_description(result));
}
} // namespace
void AudioStreamSource::Retire::operator()(AudioStreamSource* value) const noexcept {
    if (!value)
        return;
    if (value->slots_ && !value->retiring_) {
        value->retiring_ = true;
        value->slots_->retiring.fetch_add(1, std::memory_order_relaxed);
    }
    sink.retire(value, [](void* p) noexcept { delete static_cast<AudioStreamSource*>(p); });
}
AudioStreamSource::Pointer AudioStreamSource::create(ma_resource_manager& manager,
                                                     std::shared_ptr<const AudioStreamFile> file,
                                                     std::shared_ptr<AudioStreamSlots> slots,
                                                     AudioRetirement::Sink sink, bool loop,
                                                     std::uint64_t initial_frame) {
    if (!file || !slots || initial_frame >= file->info().frames)
        throw std::runtime_error(
            "Audio stream needs owned backing, slots and a valid initial frame");
    auto count = slots->reserved.load(std::memory_order_relaxed);
    for (;;) {
        if (count >= max_audio_streams) {
            if (slots->retiring.load(std::memory_order_relaxed))
                return Pointer(nullptr, Retire{std::move(sink)});
            count = slots->reserved.load(std::memory_order_relaxed);
            if (count >= max_audio_streams)
                throw std::runtime_error("Streamed audio voice limit is 8");
            continue;
        }
        if (slots->reserved.compare_exchange_weak(count, count + 1, std::memory_order_relaxed))
            break;
    }
    AudioStreamSource* raw = nullptr;
    try {
        raw = new AudioStreamSource;
    } catch (...) {
        slots->reserved.fetch_sub(1, std::memory_order_relaxed);
        throw;
    }
    Pointer value(raw, Retire{std::move(sink)});
    value->slots_ = std::move(slots);
    value->file_ = std::move(file);
    value->slots_->page_budget_bytes.fetch_add(value->page_bytes(), std::memory_order_relaxed);
    value->native_path_ = native_io_path(value->file_->path());
    auto config = ma_resource_manager_data_source_config_init();
#ifdef _WIN32
    config.pFilePathW = value->native_path_.c_str();
#else
    value->utf8_path_ = path_utf8(value->native_path_);
    config.pFilePath = value->utf8_path_.c_str();
#endif
    config.flags = MA_RESOURCE_MANAGER_DATA_SOURCE_FLAG_STREAM |
                   MA_RESOURCE_MANAGER_DATA_SOURCE_FLAG_ASYNC |
                   (loop ? MA_RESOURCE_MANAGER_DATA_SOURCE_FLAG_LOOPING : 0);
    config.initialSeekPointInPCMFrames = initial_frame;
    checked(ma_resource_manager_data_source_init_ex(&manager, &config, &value->source_),
            "Audio stream request");
    value->initialized_ = true;
    return value;
}
AudioStreamSource::~AudioStreamSource() {
    if (prepared_)
        ma_data_source_uninit(&observed_);
    if (initialized_)
        (void)ma_resource_manager_data_source_uninit(&source_);
    if (slots_) {
        if (file_)
            slots_->page_budget_bytes.fetch_sub(page_bytes(), std::memory_order_relaxed);
        slots_->reserved.fetch_sub(1, std::memory_order_relaxed);
        if (retiring_)
            slots_->retiring.fetch_sub(1, std::memory_order_relaxed);
    }
}
bool AudioStreamSource::ready() {
    if (prepared_)
        return true;
    const auto state = ma_resource_manager_data_source_result(&source_);
    if (state == MA_BUSY)
        return false;
    checked(state, "Audio stream preparation");
    ma_format format{};
    ma_uint32 channels = 0, rate = 0;
    ma_uint64 frames = 0;
    checked(ma_resource_manager_data_source_get_data_format(&source_, &format, &channels, &rate,
                                                            nullptr, 0),
            "Audio stream format");
    checked(ma_resource_manager_data_source_get_length_in_pcm_frames(&source_, &frames),
            "Audio stream duration");
    const auto expected = info();
    if (format != ma_format_f32 || rate != audio_stream_sample_rate ||
        channels != expected.channels || frames != expected.frames)
        throw std::runtime_error("Audio stream decoded format disagrees with its immutable clip");
    // Compose the native data source through its public interface. The mixer
    // records bounded atomic observations only; diagnostics belong to the owner.
    static const ma_data_source_vtable observed_table{
        [](ma_data_source* value, void* output, ma_uint64 count, ma_uint64* read) {
            auto& owner = *static_cast<ObservedSource*>(value)->owner;
            // Native pages already own looping. Observe their result before the
            // generic data-source reader can turn EOF into another queued seek.
            const auto result = ma_resource_manager_data_source_read_pcm_frames(
                &owner.source_, output, count, read);
            if (result == MA_BUSY)
                owner.underruns_.fetch_add(1, std::memory_order_relaxed);
            else if (result == MA_AT_END) {
                // Native page filling can turn a later decoder/IO failure into
                // EOF. The validated immutable clip supplies an independent end
                // boundary. A looping voice cannot finish normally at that boundary.
                ma_uint64 cursor = 0;
                if (ma_data_source_is_looping(&owner.source_) ||
                    (ma_data_source_get_cursor_in_pcm_frames(&owner.source_, &cursor) ==
                         MA_SUCCESS &&
                     cursor < owner.info().frames))
                    owner.read_error_.store(MA_INVALID_DATA, std::memory_order_relaxed);
            } else if (result != MA_SUCCESS && result != MA_AT_END)
                owner.read_error_.store(result, std::memory_order_relaxed);
            return result;
        },
        [](ma_data_source* value, ma_uint64 frame) {
            return ma_data_source_seek_to_pcm_frame(
                &static_cast<ObservedSource*>(value)->owner->source_, frame);
        },
        [](ma_data_source* value, ma_format* format, ma_uint32* channels, ma_uint32* rate,
           ma_channel* map, size_t capacity) {
            return ma_data_source_get_data_format(
                &static_cast<ObservedSource*>(value)->owner->source_, format, channels, rate, map,
                capacity);
        },
        [](ma_data_source* value, ma_uint64* cursor) {
            return ma_data_source_get_cursor_in_pcm_frames(
                &static_cast<ObservedSource*>(value)->owner->source_, cursor);
        },
        [](ma_data_source* value, ma_uint64* length) {
            return ma_data_source_get_length_in_pcm_frames(
                &static_cast<ObservedSource*>(value)->owner->source_, length);
        },
        [](ma_data_source* value, ma_bool32 loop) {
            return ma_data_source_set_looping(&static_cast<ObservedSource*>(value)->owner->source_,
                                              loop);
        },
        MA_DATA_SOURCE_SELF_MANAGED_RANGE_AND_LOOP_POINT};
    auto config = ma_data_source_config_init();
    config.vtable = &observed_table;
    checked(ma_data_source_init(&config, &observed_.base), "Audio stream observer");
    observed_.owner = this;
    prepared_ = true;
    return true;
}
ma_data_source* AudioStreamSource::data_source() {
    if (!prepared_)
        throw std::runtime_error("Audio stream has not finished preparation");
    return &observed_;
}
std::uint64_t AudioStreamSource::available_frames() const {
    if (!prepared_)
        return 0;
    ma_uint64 frames = 0;
    checked(ma_resource_manager_data_source_get_available_frames(
                const_cast<ma_resource_manager_data_source*>(&source_), &frames),
            "Audio stream pages");
    return frames;
}
std::uint64_t AudioStreamSource::cursor() const {
    if (!prepared_)
        throw std::runtime_error("Audio stream has not finished preparation");
    ma_uint64 frame = 0;
    checked(ma_resource_manager_data_source_get_cursor_in_pcm_frames(
                const_cast<ma_resource_manager_data_source*>(&source_), &frame),
            "Audio stream cursor");
    return frame;
}
std::uint64_t AudioStreamSource::page_bytes() const {
    return 2ull * audio_stream_sample_rate * info().channels * sizeof(float);
}
AudioClipInfo AudioStreamSource::info() const { return file_->info(); }
unsigned AudioStreamSource::underruns() const noexcept {
    return underruns_.load(std::memory_order_relaxed);
}
ma_result AudioStreamSource::read_error() const noexcept {
    return read_error_.load(std::memory_order_relaxed);
}
} // namespace forge::asset_detail
