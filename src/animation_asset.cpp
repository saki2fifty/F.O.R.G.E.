#include "animation_asset.hpp"
#include "animation_additive.hpp"
#include <bit>
#include <cmath>
#include <cstring>
#include <forge/transform.hpp>
#include <ozz/animation/runtime/animation.h>
#include <ozz/animation/runtime/blending_job.h>
#include <ozz/animation/runtime/ik_aim_job.h>
#include <ozz/animation/runtime/ik_two_bone_job.h>
#include <ozz/animation/runtime/local_to_model_job.h>
#include <ozz/animation/runtime/sampling_job.h>
#include <ozz/animation/runtime/skeleton.h>
#include <ozz/base/io/archive.h>
#include <ozz/base/io/stream.h>
#include <ozz/base/maths/simd_math.h>
#include <ozz/base/maths/simd_quaternion.h>
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
    std::vector<ozz::math::SoaTransform> local, blended, transition_source, additive_source;
    std::vector<ozz::math::Float4x4> model;
    std::vector<ozz::math::SimdFloat4> primary_weights, secondary_weights;
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
std::vector<Matrix> Sampler::sample_blended(float ratio, Sampler& second, float weight,
                                            std::span<const float> joint_mask, bool additive) {
    if (this == &second || impl_->skeleton != second.impl_->skeleton || !std::isfinite(weight) ||
        weight < 0 || weight > 1)
        throw ArchiveError("Invalid skeletal blend weight or skeleton compatibility");
    if (!joint_mask.empty()) {
        if (joint_mask.size() != impl_->model.size())
            throw ArchiveError("Skeletal blend mask must match the admitted joint count");
        for (const auto value : joint_mask)
            if (!std::isfinite(value) || value < 0 || value > 1)
                throw ArchiveError("Skeletal blend mask values must be finite in [0,1]");
        impl_->primary_weights.resize(impl_->local.size());
        impl_->secondary_weights.resize(impl_->local.size());
        for (std::size_t i = 0; i < impl_->local.size(); ++i) {
            float primary[4], secondary[4];
            for (unsigned lane = 0; lane < 4; ++lane) {
                const auto joint = i * 4 + lane;
                secondary[lane] = joint < joint_mask.size() ? weight * joint_mask[joint] : 0;
                primary[lane] = 1 - secondary[lane];
            }
            impl_->primary_weights[i] = ozz::math::simd_float4::LoadPtrU(primary);
            impl_->secondary_weights[i] = ozz::math::simd_float4::LoadPtrU(secondary);
        }
    }
    sample_local(ratio);
    second.sample_local(ratio);
    impl_->blended.resize(impl_->local.size());
    ozz::animation::BlendingJob::Layer layers[] = {
        {1 - weight, ozz::make_span(impl_->local), {}},
        {weight, ozz::make_span(second.impl_->local), {}}};
    if (!joint_mask.empty()) {
        layers[0].weight = layers[1].weight = 1;
        layers[0].joint_weights = ozz::make_span(impl_->primary_weights);
        layers[1].joint_weights = ozz::make_span(impl_->secondary_weights);
    }
    ozz::animation::BlendingJob blend;
    blend.layers = ozz::make_span(layers);
    ozz::animation::BlendingJob::Layer additive_layer;
    if (additive) {
        auto& source = impl_->additive_source;
        source.resize(impl_->local.size());
        for (std::size_t i = 0; i < source.size(); ++i) {
            float values[10][4];
            for (unsigned lane = 0; lane < 4; ++lane) {
                const auto joint = i * 4 + lane;
                LocalPose delta;
                if (joint < impl_->model.size() && weight != 0 &&
                    (joint_mask.empty() || joint_mask[joint] != 0))
                    delta = additive_delta(pose_lane(second.impl_->local[i], lane),
                                           impl_->skeleton->joint_rest_pose(joint));
                for (unsigned j = 0; j < 3; ++j) {
                    values[j][lane] = delta.translation[j];
                    values[7 + j][lane] = delta.scale[j];
                }
                for (unsigned j = 0; j < 4; ++j)
                    values[3 + j][lane] = delta.rotation[j];
            }
            source[i].translation = {ozz::math::simd_float4::LoadPtrU(values[0]),
                                     ozz::math::simd_float4::LoadPtrU(values[1]),
                                     ozz::math::simd_float4::LoadPtrU(values[2])};
            source[i].rotation = {ozz::math::simd_float4::LoadPtrU(values[3]),
                                  ozz::math::simd_float4::LoadPtrU(values[4]),
                                  ozz::math::simd_float4::LoadPtrU(values[5]),
                                  ozz::math::simd_float4::LoadPtrU(values[6])};
            source[i].scale = {ozz::math::simd_float4::LoadPtrU(values[7]),
                               ozz::math::simd_float4::LoadPtrU(values[8]),
                               ozz::math::simd_float4::LoadPtrU(values[9])};
        }
        layers[0] = {1, ozz::make_span(impl_->local), {}};
        blend.layers = {layers, 1};
        additive_layer = {weight, ozz::make_span(source), {}};
        if (!joint_mask.empty()) {
            additive_layer.weight = 1;
            additive_layer.joint_weights = ozz::make_span(impl_->secondary_weights);
        }
        blend.additive_layers = {&additive_layer, 1};
    }
    blend.rest_pose = impl_->skeleton->impl_->value.joint_rest_poses();
    blend.output = ozz::make_span(impl_->blended);
    if (!blend.Run())
        throw ArchiveError("Ozz skeletal blending failed");
    impl_->local.swap(impl_->blended);
    return evaluate_model();
}
std::vector<Matrix> Sampler::sample_weighted(std::span<const WeightedAnimationSample> samples) {
    if (samples.empty() || samples.size() > 3 || samples.front().sampler != this)
        throw ArchiveError("Weighted animation sampling requires one to three layers");
    double total = 0;
    for (std::size_t i = 0; i < samples.size(); ++i) {
        const auto& layer = samples[i];
        if (!layer.sampler || layer.sampler->impl_->skeleton != impl_->skeleton ||
            !std::isfinite(layer.ratio) || layer.ratio < 0 || layer.ratio > 1 ||
            !std::isfinite(layer.weight) || layer.weight < 0 || layer.weight > 1)
            throw ArchiveError("Invalid weighted animation layer or skeleton compatibility");
        for (std::size_t j = 0; j < i; ++j)
            if (samples[j].sampler == layer.sampler)
                throw ArchiveError("Weighted animation layers require independent samplers");
        total += layer.weight;
    }
    if (std::abs(total - 1) > 1e-5)
        throw ArchiveError("Weighted animation influences must sum to one");
    if (samples.size() == 1)
        return sample(samples.front().ratio);
    std::array<ozz::animation::BlendingJob::Layer, 3> layers{};
    for (std::size_t i = 0; i < samples.size(); ++i) {
        samples[i].sampler->sample_local(samples[i].ratio);
        layers[i] = {samples[i].weight, ozz::make_span(samples[i].sampler->impl_->local), {}};
    }
    impl_->blended.resize(impl_->local.size());
    ozz::animation::BlendingJob blend;
    blend.layers = {layers.data(), samples.size()};
    blend.rest_pose = impl_->skeleton->impl_->value.joint_rest_poses();
    blend.output = ozz::make_span(impl_->blended);
    if (!blend.Run())
        throw ArchiveError("Ozz weighted skeletal blending failed");
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
std::vector<Matrix> Sampler::constrain(const RigConstraints& constraints,
                                       std::span<const LocalPoseOverride> base) {
    if (!impl_->sampled)
        throw ArchiveError("IK requires an evaluated animation pose");
    std::vector<Matrix> result;
    if (!base.empty()) {
        if (base.size() != impl_->model.size())
            throw ArchiveError("IK base pose differs from the exact skeleton");
        for (std::size_t index = 0; index < base.size(); ++index) {
            auto& soa = impl_->local[index / 4];
            const auto lane = unsigned(index % 4);
            const auto replace = [&](auto& field, float value) {
                if (!std::isfinite(value))
                    throw ArchiveError("IK base pose is nonfinite");
                float lanes[4];
                ozz::math::StorePtrU(field, lanes);
                lanes[lane] = value;
                field = ozz::math::simd_float4::LoadPtrU(lanes);
            };
            const auto& override = base[index];
            if (override.channels & 1) {
                replace(soa.translation.x, override.value.translation[0]);
                replace(soa.translation.y, override.value.translation[1]);
                replace(soa.translation.z, override.value.translation[2]);
            }
            if (override.channels & 2) {
                replace(soa.rotation.x, override.value.rotation[0]);
                replace(soa.rotation.y, override.value.rotation[1]);
                replace(soa.rotation.z, override.value.rotation[2]);
                replace(soa.rotation.w, override.value.rotation[3]);
            }
            if (override.channels & 4) {
                replace(soa.scale.x, override.value.scale[0]);
                replace(soa.scale.y, override.value.scale[1]);
                replace(soa.scale.z, override.value.scale[2]);
            }
        }
        result = evaluate_model();
    }
    const auto checked_matrix = [&](std::size_t index) -> const ozz::math::Float4x4& {
        if (index >= impl_->model.size())
            throw ArchiveError("IK joint is outside its exact skeleton");
        const auto& matrix = impl_->model[index];
        AffineTransform affine;
        for (unsigned c = 0; c < 4; ++c) {
            float lane[4];
            ozz::math::StorePtrU(matrix.cols[c], lane);
            for (unsigned r = 0; r < 3; ++r)
                affine.m[r * 4 + c] = lane[r];
        }
        if (inverse_reciprocal_condition(affine) < min_inverse_rcond)
            throw ArchiveError("IK transform inverse unavailable: singular or ill-conditioned");
        ozz::math::SimdInt4 invertible;
        const auto inverse = ozz::math::Invert(matrix, &invertible);
        if (!ozz::math::AreAllTrue1(invertible))
            throw ArchiveError("IK transform inverse unavailable in native precision");
        for (const auto& column : inverse.cols) {
            float values[4];
            ozz::math::StorePtrU(column, values);
            for (float value : values)
                if (!std::isfinite(value))
                    throw ArchiveError("IK native inverse is nonfinite");
        }
        // A native model-space rotation must remain a local rotation with scale
        // preserved. Unequal scale or shear cannot satisfy that contract.
        double lengths[3]{};
        for (unsigned c = 0; c < 3; ++c)
            for (unsigned r = 0; r < 3; ++r)
                lengths[c] += double(affine.m[r * 4 + c]) * affine.m[r * 4 + c];
        const auto largest = std::max({lengths[0], lengths[1], lengths[2]});
        for (unsigned c = 0; c < 3; ++c) {
            if (std::abs(lengths[c] - largest) > largest * 1e-5)
                throw ArchiveError("IK requires uniform scale magnitudes on joint bases");
            for (unsigned d = c + 1; d < 3; ++d) {
                double dot = 0;
                for (unsigned r = 0; r < 3; ++r)
                    dot += double(affine.m[r * 4 + c]) * affine.m[r * 4 + d];
                if (std::abs(dot) > largest * 1e-5)
                    throw ArchiveError("IK cannot preserve a sheared joint basis");
            }
        }
        return matrix;
    };
    const auto vector = [](const std::array<float, 3>& value) {
        return ozz::math::simd_float4::Load(value[0], value[1], value[2], 0);
    };
    const auto stable_plane = [&](const auto& matrix, const auto& target, const auto& pole) {
        float position[4];
        ozz::math::StorePtrU(matrix.cols[3], position);
        Double3 direction;
        double length = 0, cross = 0, pole_length = 0;
        for (unsigned i = 0; i < 3; ++i) {
            direction[i] = double(target[i]) - position[i];
            length += direction[i] * direction[i];
            pole_length += double(pole[i]) * pole[i];
        }
        for (unsigned i = 0; i < 3; ++i) {
            const auto v = direction[(i + 1) % 3] * pole[(i + 2) % 3] -
                           direction[(i + 2) % 3] * pole[(i + 1) % 3];
            cross += v * v;
        }
        if (!std::isfinite(length) || length < 1e-12 || cross < length * pole_length * 1e-8)
            throw ArchiveError("IK target is too close or parallel to its pole direction");
    };
    const auto rotate = [&](std::size_t index, const auto& correction) {
        auto& soa = impl_->local[index / 4];
        const auto lane = unsigned(index % 4);
        const auto pose = pose_lane(soa, lane);
        float q[4];
        ozz::math::StorePtrU(correction.xyzw, q);
        // Conjugate through signed uniform scale: D Q D is a rotation, while
        // R D Q alone would move a reflected local basis in the wrong direction.
        const float sx = std::signbit(pose.scale[0]) ? -1.f : 1.f;
        const float sy = std::signbit(pose.scale[1]) ? -1.f : 1.f;
        const float sz = std::signbit(pose.scale[2]) ? -1.f : 1.f;
        const auto magnitude =
            std::max({std::abs(pose.scale[0]), std::abs(pose.scale[1]), std::abs(pose.scale[2])});
        for (float scale : pose.scale)
            if (std::abs(std::abs(scale) - magnitude) > magnitude * 1e-5f)
                throw ArchiveError("IK cannot preserve nonuniform local scale");
        q[0] *= sy * sz;
        q[1] *= sx * sz;
        q[2] *= sx * sy;
        const auto result = root_compose(
            {{}, {pose.rotation[0], pose.rotation[1], pose.rotation[2], pose.rotation[3]}},
            {{}, {q[0], q[1], q[2], q[3]}});
        auto replace = [&](auto& field, float value) {
            float lanes[4];
            ozz::math::StorePtrU(field, lanes);
            lanes[lane] = value;
            field = ozz::math::simd_float4::LoadPtrU(lanes);
        };
        replace(soa.rotation.x, float(result.rotation[0]));
        replace(soa.rotation.y, float(result.rotation[1]));
        replace(soa.rotation.z, float(result.rotation[2]));
        replace(soa.rotation.w, float(result.rotation[3]));
    };
    if (constraints.reach) {
        const auto& value = *constraints.reach;
        const auto& start = checked_matrix(value.start);
        const auto& middle = checked_matrix(value.middle);
        const auto& end = checked_matrix(value.end);
        stable_plane(start, value.target, value.pole);
        for (const auto* from : {&start, &middle}) {
            const auto* to = from == &start ? &middle : &end;
            float a[4], b[4];
            ozz::math::StorePtrU(from->cols[3], a);
            ozz::math::StorePtrU(to->cols[3], b);
            double length = 0;
            for (unsigned i = 0; i < 3; ++i)
                length += (double(a[i]) - b[i]) * (double(a[i]) - b[i]);
            if (!std::isfinite(length) || length < 1e-12)
                throw ArchiveError("IK chain contains a zero-length bone");
        }
        ozz::animation::IKTwoBoneJob job;
        job.start_joint = &start;
        job.mid_joint = &middle;
        job.end_joint = &end;
        job.target = vector(value.target);
        job.pole_vector = vector(value.pole);
        auto axis = value.axis;
        const auto middle_pose = pose_lane(impl_->local[value.middle / 4], value.middle % 4);
        const float sx = std::signbit(middle_pose.scale[0]) ? -1.f : 1.f;
        const float sy = std::signbit(middle_pose.scale[1]) ? -1.f : 1.f;
        const float sz = std::signbit(middle_pose.scale[2]) ? -1.f : 1.f;
        axis[0] *= sy * sz;
        axis[1] *= sx * sz;
        axis[2] *= sx * sy;
        job.mid_axis = vector(axis);
        job.weight = value.weight;
        job.soften = value.soften;
        job.twist_angle = value.twist;
        ozz::math::SimdQuaternion start_rotation, middle_rotation;
        job.start_joint_correction = &start_rotation;
        job.mid_joint_correction = &middle_rotation;
        if (!job.Run())
            throw ArchiveError("Native two-bone IK rejected its prepared inputs");
        rotate(value.start, start_rotation);
        rotate(value.middle, middle_rotation);
        result = evaluate_model();
    }
    if (constraints.aim) {
        const auto& value = *constraints.aim;
        const auto& joint = checked_matrix(value.joint);
        stable_plane(joint, value.target, value.pole);
        ozz::animation::IKAimJob job;
        job.joint = &joint;
        job.target = vector(value.target);
        job.pole_vector = vector(value.pole);
        job.forward = vector(value.forward);
        job.up = vector(value.up);
        job.weight = value.weight;
        job.twist_angle = value.twist;
        ozz::math::SimdQuaternion correction;
        job.joint_correction = &correction;
        if (!job.Run())
            throw ArchiveError("Native aim IK rejected its prepared inputs");
        rotate(value.joint, correction);
        result = evaluate_model();
    }
    return result;
}
} // namespace forge::animation_detail
