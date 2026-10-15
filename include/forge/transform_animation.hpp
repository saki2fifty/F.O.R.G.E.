#pragma once
#include <forge/animation_components.hpp>
#include <forge/animation_property.hpp>
#include <forge/material_parameter.hpp>
#include <forge/transform.hpp>
#include <nlohmann/json.hpp>
#include <optional>
#include <span>
#include <string>
#include <vector>
namespace forge {
inline constexpr std::size_t transform_animation_byte_limit = 2 * 1024 * 1024;
inline constexpr std::size_t transform_animation_cooked_byte_limit =
    transform_animation_byte_limit + 128; // Versioned envelope around the original source.
inline constexpr std::int64_t animation_timebase = 1000000;
struct TransformAnimationTangents {
    // Local vector units per second. Rotation retains quaternion slerp.
    Double3 translation{}, scale{};
};
struct AnimationPropertyCurve {
    bool cubic = false;
    double incoming = 0, outgoing = 0; // Native property units per second.
};
struct AnimationMaterialBinding {
    std::string slot, key;
    MaterialParameterType type = MaterialParameterType::Scalar;
    bool operator==(const AnimationMaterialBinding&) const = default;
};
using AnimationMaterialValues = std::array<double, 4>;
struct AnimationMaterialKey {
    AnimationMaterialValues values{};
    std::array<AnimationPropertyCurve, 4> curves{};
};
struct AnimationMaterialCurveEdit {
    AnimationMaterialBinding binding;
    unsigned lane = 0;
    AnimationPropertyCurve value;
};
struct TransformAnimationCurves {
    bool cubic = false;
    TransformAnimationTangents incoming, outgoing;
    unsigned property_mask = 0;
    std::array<AnimationPropertyCurve, animation_property_count> properties{};
};
enum class TransformAnimationTangentPreset { Flat, Linear, Smooth };
struct TransformAnimationKey {
    AssetId id;
    std::int64_t time = 0;
    LocalTransform pose;
    bool held = false;
    TransformAnimationCurves curves;
    AnimationPropertyValues properties{};
    // Empty for legacy clips. Compiled ordinals belong only to this clip revision.
    std::vector<AnimationMaterialKey> materials;
};
struct TransformAnimationClip {
    AssetId asset;
    std::int64_t duration = 0;
    unsigned channels = 7;
    std::vector<TransformAnimationKey> keys;
    unsigned properties = 0;
    std::vector<AnimationMaterialBinding> material_bindings;
    LocalTransform sample(double microseconds) const;
    AnimationPropertyValues sample_properties(double microseconds) const;
    // Reusable caller storage; target-specific material admission precedes writes.
    void sample_materials(double microseconds, std::span<AnimationMaterialValues>) const;
    // Exact endpoint/held/linear/cubic extrema over the whole admitted clip.
    // Computed during target admission, never required on the fixed sampling path.
    std::array<double, 2> material_range(std::size_t binding, unsigned lane) const;
    std::size_t resident_bytes() const;
};
struct TransformAnimationSource {
    nlohmann::json document;
    static TransformAnimationSource create(AssetId);
    static TransformAnimationSource parse(std::span<const std::byte>);
    AssetId asset() const;
    void validate() const;
    TransformAnimationClip clip() const;
    std::vector<std::byte> cook() const;
    AssetId set_key(std::int64_t time, const LocalTransform&, bool held = false, AssetId key = {},
                    std::optional<TransformAnimationCurves> curves = {},
                    std::optional<AnimationPropertyValues> properties = {},
                    std::optional<std::span<const AnimationMaterialValues>> materials = {});
    void add_material_parameter(const AnimationMaterialBinding&, const AnimationMaterialValues&);
    void erase_material_parameter(std::string_view slot, std::string_view parameter);
    void set_material_value(AssetId key, std::string_view slot, std::string_view parameter,
                            const AnimationMaterialValues&);
    void set_material_curve(AssetId key, std::string_view slot, std::string_view parameter,
                            unsigned lane, const AnimationPropertyCurve&);
    void set_material_curves(std::span<const AssetId>, std::string_view slot,
                             std::string_view parameter, unsigned lane,
                             const AnimationPropertyCurve&);
    void set_material_curves(AssetId, std::span<const AnimationMaterialCurveEdit>);
    void set_material_tangent_preset(std::span<const AssetId>, std::string_view slot,
                                     std::string_view parameter, unsigned lane,
                                     TransformAnimationTangentPreset);
    void add_property(AnimationProperty, double initial_value);
    void erase_property(AnimationProperty);
    void set_property_curve(AssetId key, AnimationProperty, const AnimationPropertyCurve&);
    void set_property_tangent_preset(std::span<const AssetId>, AnimationProperty,
                                     TransformAnimationTangentPreset);
    void erase_key(AssetId);
    void move_key(AssetId, std::int64_t time);
    // Atomic final-state edits; complete key metadata and stable IDs are retained.
    void move_keys(std::span<const AssetId>, std::int64_t offset_us);
    std::vector<AssetId> duplicate_keys(std::span<const AssetId>, std::int64_t offset_us);
    void erase_keys(std::span<const AssetId>);
    // Whole-clip retiming rescales known slopes; selected frame snapping keeps them.
    void retime_duration(std::int64_t duration_us);
    void snap_keys_to_frames(std::span<const AssetId>, unsigned frames_per_second);
    // Affine local position values and multiplicative sizes; rescale known slopes.
    void transform_key_values(std::span<const AssetId>, Double3 position_multiplier,
                              Double3 position_offset, Double3 scale_multiplier);
    // Atomic selected-key slopes from the original neighboring poses/times.
    void set_tangent_preset(std::span<const AssetId>, TransformAnimationTangentPreset);
};
TransformAnimationClip decode_transform_animation(std::span<const std::byte>, AssetId);
nlohmann::json animation_pose_json(const LocalTransform&);
LocalTransform animation_pose(const nlohmann::json&);
} // namespace forge
