#include "animation_asset.hpp"
#include <bit>
#include <cmath>
#include <cstring>
#include <ozz/animation/runtime/animation.h>
#include <ozz/animation/runtime/blending_job.h>
#include <ozz/animation/runtime/local_to_model_job.h>
#include <ozz/animation/runtime/sampling_job.h>
#include <ozz/animation/runtime/skeleton.h>
#include <ozz/base/io/archive.h>
#include <ozz/base/io/stream.h>
#include <ozz/base/maths/simd_math.h>
#include <ozz/base/maths/soa_transform.h>
namespace forge::animation_detail {
static_assert(std::endian::native == std::endian::little,
              "FORGE Ozz admission currently supports little-endian hosts");
namespace {
LocalPose pose_lane(const ozz::math::SoaTransform& local, unsigned lane) {
    float values[10][4];
    ozz::math::StorePtrU(local.translation.x, values[0]);
    ozz::math::StorePtrU(local.translation.y, values[1]);
    ozz::math::StorePtrU(local.translation.z, values[2]);
    ozz::math::StorePtrU(local.rotation.x, values[3]);
    ozz::math::StorePtrU(local.rotation.y, values[4]);
    ozz::math::StorePtrU(local.rotation.z, values[5]);
    ozz::math::StorePtrU(local.rotation.w, values[6]);
    ozz::math::StorePtrU(local.scale.x, values[7]);
    ozz::math::StorePtrU(local.scale.y, values[8]);
    ozz::math::StorePtrU(local.scale.z, values[9]);
    for (const auto& row : values)
        if (!std::isfinite(row[lane]))
            throw ArchiveError("Animation root local pose is not finite");
    return {{values[0][lane], values[1][lane], values[2][lane]},
            {values[7][lane], values[8][lane], values[9][lane]},
            {values[3][lane], values[4][lane], values[5][lane], values[6][lane]}};
}
class ReadOnlyStream final : public ozz::io::Stream {
  public:
    explicit ReadOnlyStream(std::span<const std::byte> data) : data_(data) {}
    bool opened() const override { return true; }
    std::size_t Read(void* out, std::size_t size) override {
        if (size > data_.size() - at_)
            throw ArchiveError("Admitted archive read exceeds bounds");
        if (size)
            std::memcpy(out, data_.data() + at_, size);
        at_ += size;
        return size;
    }
    std::size_t Write(const void*, std::size_t) override {
        throw ArchiveError("Read-only archive");
    }
    int Seek(int offset, Origin origin) override {
        const auto base = origin == kSet ? 0 : origin == kEnd ? data_.size() : at_;
        const auto next = static_cast<std::int64_t>(base) + offset;
        if (next < 0 || static_cast<std::uint64_t>(next) > data_.size())
            return -1;
        at_ = static_cast<std::size_t>(next);
        return 0;
    }
    int Tell() const override { return static_cast<int>(at_); }
    std::size_t Size() const override { return data_.size(); }

  private:
    std::span<const std::byte> data_;
    std::size_t at_ = 0;
};
template <class T> void load(T& out, std::span<const std::byte> bytes) {
    ReadOnlyStream stream(bytes);
    ozz::io::IArchive archive(&stream);
    if (!archive.TestTag<T>())
        throw ArchiveError("Admitted Ozz type mismatch");
    archive >> out;
    if (std::size_t(stream.Tell()) != bytes.size())
        throw ArchiveError("Ozz layout differs from validator");
}
} // namespace
struct Skeleton::Impl {
    ozz::animation::Skeleton value;
};
struct Clip::Impl {
    ozz::animation::Animation value;
};
Skeleton::Skeleton(std::span<const std::byte> bytes) {
    info_ = validate_archive(bytes, ArchiveKind::Skeleton);
    impl_ = std::make_unique<Impl>();
    load(impl_->value, bytes);
    if (impl_->value.num_joints() != int(info_.tracks))
        throw ArchiveError("Ozz skeleton count mismatch");
}
Skeleton::~Skeleton() = default;
std::size_t Skeleton::resident_bytes() const {
    const auto& native = impl_->value;
    std::size_t bytes = sizeof(*this) + sizeof(Impl) +
                        info_.parents.capacity() * sizeof(std::int16_t) +
                        native.joint_rest_poses().size_bytes() +
                        native.joint_parents().size_bytes() + native.joint_names().size_bytes();
    for (const auto* name : native.joint_names())
        bytes += std::strlen(name) + 1;
    return bytes;
}
std::vector<std::string> Skeleton::joint_names() const {
    std::vector<std::string> result;
    for (const auto* name : impl_->value.joint_names())
        result.emplace_back(name);
    return result;
}
std::vector<Matrix> Skeleton::rest_pose() const {
    std::vector<ozz::math::Float4x4> matrices(info_.tracks);
    ozz::animation::LocalToModelJob job;
    job.skeleton = &impl_->value;
    job.input = impl_->value.joint_rest_poses();
    job.output = ozz::make_span(matrices);
    if (!job.Run())
        throw ArchiveError("Ozz rest pose evaluation failed");
    std::vector<Matrix> result(matrices.size());
    for (std::size_t i = 0; i < matrices.size(); ++i) {
        for (unsigned c = 0; c < 4; ++c)
            ozz::math::StorePtrU(matrices[i].cols[c], result[i].data() + c * 4);
        for (auto value : result[i])
            if (!std::isfinite(value))
                throw ArchiveError("Skeleton rest model pose is not finite");
    }
    return result;
}
LocalPose Skeleton::joint_rest_pose(std::size_t joint) const {
    if (joint >= info_.tracks)
        throw ArchiveError("Root joint exceeds its admitted skeleton");
    return pose_lane(impl_->value.joint_rest_poses()[joint / 4], unsigned(joint % 4));
}
Clip::Clip(std::span<const std::byte> bytes) {
    info_ = validate_archive(bytes, ArchiveKind::Animation);
    impl_ = std::make_unique<Impl>();
    load(impl_->value, bytes);
    if (impl_->value.num_tracks() != int(info_.tracks))
        throw ArchiveError("Ozz clip count mismatch");
}
Clip::~Clip() = default;
std::size_t Clip::resident_bytes() const {
    // Pinned Animation::size() includes its object and key buffers, but not its name.
    const auto length = std::strlen(impl_->value.name());
    return sizeof(*this) + sizeof(Impl) - sizeof(ozz::animation::Animation) + impl_->value.size() +
           (length ? length + 1 : 0) + info_.parents.capacity() * sizeof(std::int16_t);
}
struct Sampler::Impl {
    std::shared_ptr<const Skeleton> skeleton;
    std::shared_ptr<const Clip> clip;
    ozz::animation::SamplingJob::Context context;
    std::vector<ozz::math::SoaTransform> local, blended, transition_source;
    std::vector<ozz::math::Float4x4> model;
    bool sampled = false;
    std::optional<std::size_t> root_joint;
    std::optional<LocalPose> raw_root;
    LocalPose rest_root;
    std::uint64_t sampling_jobs = 0;
    Impl(std::shared_ptr<const Skeleton> s, std::shared_ptr<const Clip> c)
        : skeleton(std::move(s)), clip(std::move(c)), context(int(skeleton->info().tracks)),
          local((skeleton->info().tracks + 3) / 4), model(skeleton->info().tracks) {}
};
Sampler::Sampler(std::shared_ptr<const Skeleton> s, std::shared_ptr<const Clip> c) {
    if (!s || !c || s->info().tracks != c->info().tracks)
        throw ArchiveError("Skeleton and clip track counts differ");
    impl_ = std::make_unique<Impl>(std::move(s), std::move(c));
}
Sampler::~Sampler() = default;
void Sampler::reset() {
    impl_->context.Invalidate();
    impl_->sampled = false;
    impl_->raw_root.reset();
}
void Sampler::root_motion_joint(std::optional<std::size_t> joint) {
    if (joint == impl_->root_joint)
        return;
    if (joint &&
        (*joint >= impl_->model.size() || impl_->skeleton->info().parents.at(*joint) != -1))
        throw ArchiveError("Root motion requires an admitted skeleton root joint");
    if (joint)
        impl_->rest_root = impl_->skeleton->joint_rest_pose(*joint);
    impl_->root_joint = joint;
    impl_->raw_root.reset();
}
std::optional<LocalPose> Sampler::sampled_root() const { return impl_->raw_root; }
std::uint64_t Sampler::sampling_jobs() const { return impl_->sampling_jobs; }
void Sampler::sample_local(float ratio) {
    impl_->sampled = false;
    if (!std::isfinite(ratio) || ratio < 0 || ratio > 1)
        throw ArchiveError("Invalid sample ratio");
    ozz::animation::SamplingJob sample;
    sample.animation = &impl_->clip->impl_->value;
    sample.context = &impl_->context;
    sample.ratio = ratio;
    sample.output = ozz::make_span(impl_->local);
    if (!sample.Run())
        throw ArchiveError("Ozz animation sampling failed");
    ++impl_->sampling_jobs;
    if (impl_->root_joint)
        impl_->raw_root =
            pose_lane(impl_->local[*impl_->root_joint / 4], unsigned(*impl_->root_joint % 4));
}
std::vector<Matrix> Sampler::sample(float ratio) {
    sample_local(ratio);
    return evaluate_model();
}
std::vector<Matrix> Sampler::sample_blended(float ratio, Sampler& second, float weight) {
    if (this == &second || impl_->skeleton != second.impl_->skeleton || !std::isfinite(weight) ||
        weight < 0 || weight > 1)
        throw ArchiveError("Invalid skeletal blend weight or skeleton compatibility");
    sample_local(ratio);
    second.sample_local(ratio);
    impl_->blended.resize(impl_->local.size());
    const ozz::animation::BlendingJob::Layer layers[] = {
        {1 - weight, ozz::make_span(impl_->local), {}},
        {weight, ozz::make_span(second.impl_->local), {}}};
    ozz::animation::BlendingJob blend;
    blend.layers = ozz::make_span(layers);
    blend.rest_pose = impl_->skeleton->impl_->value.joint_rest_poses();
    blend.output = ozz::make_span(impl_->blended);
    if (!blend.Run())
        throw ArchiveError("Ozz skeletal blending failed");
    impl_->local.swap(impl_->blended);
    return evaluate_model();
}
std::vector<Matrix> Sampler::sample_transition(float ratio, Sampler& outgoing, float outgoing_ratio,
                                               float weight) {
    if (this == &outgoing || impl_->skeleton != outgoing.impl_->skeleton)
        throw ArchiveError("State transition skeleton mismatch");
    if (!std::isfinite(weight) || weight < 0 || weight > 1)
        throw ArchiveError("Invalid state transition weight");
    outgoing.sample_local(outgoing_ratio);
    sample_local(ratio);
    impl_->blended.resize(impl_->local.size());
    const ozz::animation::BlendingJob::Layer layers[] = {
        {1 - weight, ozz::make_span(outgoing.impl_->local), {}},
        {weight, ozz::make_span(impl_->local), {}}};
    ozz::animation::BlendingJob blend;
    blend.layers = ozz::make_span(layers);
    blend.rest_pose = impl_->skeleton->impl_->value.joint_rest_poses();
    blend.output = ozz::make_span(impl_->blended);
    if (!blend.Run())
        throw ArchiveError("Ozz state transition blending failed");
    impl_->local.swap(impl_->blended);
    return evaluate_model();
}
std::vector<Matrix> Sampler::sample_transition(float ratio, std::span<const LocalPose> from,
                                               float weight) {
    if (from.size() != impl_->model.size() || !std::isfinite(weight) || weight < 0 || weight > 1)
        throw ArchiveError("State transition local pose/weight mismatch");
    sample_local(ratio);
    auto& source = impl_->transition_source;
    source.resize(impl_->local.size());
    for (std::size_t i = 0; i < source.size(); ++i) {
        float values[10][4]{};
        for (unsigned lane = 0; lane < 4; ++lane) {
            LocalPose p;
            if (i * 4 + lane < from.size())
                p = from[i * 4 + lane];
            double norm = 0;
            for (unsigned j = 0; j < 4; ++j)
                norm += double(p.rotation[j]) * p.rotation[j];
            if (!std::isfinite(norm) || std::abs(norm - 1) > 1e-4)
                throw ArchiveError("State transition quaternion is not normalized");
            for (unsigned j = 0; j < 3; ++j) {
                values[j][lane] = p.translation[j];
                values[7 + j][lane] = p.scale[j];
            }
            for (unsigned j = 0; j < 4; ++j)
                values[3 + j][lane] = p.rotation[j];
            for (const auto& v : values)
                if (!std::isfinite(v[lane]))
                    throw ArchiveError("State transition local pose is not finite");
        }
        auto& p = source[i];
        p.translation = {ozz::math::simd_float4::LoadPtrU(values[0]),
                         ozz::math::simd_float4::LoadPtrU(values[1]),
                         ozz::math::simd_float4::LoadPtrU(values[2])};
        p.rotation = {ozz::math::simd_float4::LoadPtrU(values[3]),
                      ozz::math::simd_float4::LoadPtrU(values[4]),
                      ozz::math::simd_float4::LoadPtrU(values[5]),
                      ozz::math::simd_float4::LoadPtrU(values[6])};
        p.scale = {ozz::math::simd_float4::LoadPtrU(values[7]),
                   ozz::math::simd_float4::LoadPtrU(values[8]),
                   ozz::math::simd_float4::LoadPtrU(values[9])};
    }
    impl_->blended.resize(impl_->local.size());
    const ozz::animation::BlendingJob::Layer layers[] = {
        {1 - weight, ozz::make_span(source), {}}, {weight, ozz::make_span(impl_->local), {}}};
    ozz::animation::BlendingJob blend;
    blend.layers = ozz::make_span(layers);
    blend.rest_pose = impl_->skeleton->impl_->value.joint_rest_poses();
    blend.output = ozz::make_span(impl_->blended);
    if (!blend.Run())
        throw ArchiveError("Ozz state transition blending failed");
    impl_->local.swap(impl_->blended);
    return evaluate_model();
}
std::vector<Matrix> Sampler::evaluate_model() {
    if (impl_->root_joint) {
        auto& local = impl_->local[*impl_->root_joint / 4];
        const auto lane = unsigned(*impl_->root_joint % 4);
        auto replace = [&](auto& field, float value) {
            float values[4];
            ozz::math::StorePtrU(field, values);
            values[lane] = value;
            field = ozz::math::simd_float4::LoadPtrU(values);
        };
        replace(local.translation.x, impl_->rest_root.translation[0]);
        replace(local.translation.y, impl_->rest_root.translation[1]);
        replace(local.translation.z, impl_->rest_root.translation[2]);
        replace(local.rotation.x, impl_->rest_root.rotation[0]);
        replace(local.rotation.y, impl_->rest_root.rotation[1]);
        replace(local.rotation.z, impl_->rest_root.rotation[2]);
        replace(local.rotation.w, impl_->rest_root.rotation[3]);
    }
    ozz::animation::LocalToModelJob model;
    model.skeleton = &impl_->skeleton->impl_->value;
    model.input = ozz::make_span(impl_->local);
    model.output = ozz::make_span(impl_->model);
    if (!model.Run())
        throw ArchiveError("Ozz local-to-model evaluation failed");
    std::vector<Matrix> result(impl_->model.size());
    for (std::size_t i = 0; i < result.size(); ++i) {
        for (unsigned col = 0; col < 4; ++col)
            ozz::math::StorePtrU(impl_->model[i].cols[col], result[i].data() + col * 4);
        for (auto value : result[i])
            if (!std::isfinite(value))
                throw ArchiveError("Animation model pose is not finite");
    }
    impl_->sampled = true;
    return result;
}
std::vector<LocalPose> Sampler::local_pose() const {
    if (!impl_->sampled)
        throw ArchiveError("No successful animation sample is available");
    std::vector<LocalPose> result(impl_->model.size());
    for (std::size_t i = 0; i < impl_->local.size(); ++i) {
        const auto& local = impl_->local[i];
        float values[10][4];
        ozz::math::StorePtrU(local.translation.x, values[0]);
        ozz::math::StorePtrU(local.translation.y, values[1]);
        ozz::math::StorePtrU(local.translation.z, values[2]);
        ozz::math::StorePtrU(local.rotation.x, values[3]);
        ozz::math::StorePtrU(local.rotation.y, values[4]);
        ozz::math::StorePtrU(local.rotation.z, values[5]);
        ozz::math::StorePtrU(local.rotation.w, values[6]);
        ozz::math::StorePtrU(local.scale.x, values[7]);
        ozz::math::StorePtrU(local.scale.y, values[8]);
        ozz::math::StorePtrU(local.scale.z, values[9]);
        for (unsigned lane = 0; lane < 4 && i * 4 + lane < result.size(); ++lane) {
            for (const auto& row : values)
                if (!std::isfinite(row[lane]))
                    throw ArchiveError("Animation local pose is not finite");
            auto& output = result[i * 4 + lane];
            output.translation = {values[0][lane], values[1][lane], values[2][lane]};
            output.rotation = {values[3][lane], values[4][lane], values[5][lane], values[6][lane]};
            output.scale = {values[7][lane], values[8][lane], values[9][lane]};
        }
    }
    return result;
}
} // namespace forge::animation_detail
