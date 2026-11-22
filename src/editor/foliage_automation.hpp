#pragma once
#include "../foliage_collision.hpp"
#include "../terrain_foliage_authoring.hpp"
#include "automation_schema.hpp"
#include "document.hpp"
namespace forge {
inline std::vector<LiveAuthoringMethod>
foliage_automation_methods(Scene& scene, SceneDocument& project,
                           FoliageCollisionPreparation* clearance = nullptr) {
    std::vector<LiveAuthoringMethod> methods;
    Json scatter_schema;
    for (const auto& command : authoring_commands())
        if (command.at("id") == "mesh.scatter")
            scatter_schema = command.at("input_schema");
    scatter_schema["properties"].erase("world_heights");
    scatter_schema["properties"].erase("placement_mask");
    scatter_schema["properties"].erase("world_normals");
    for (bool mesh_surface : {false, true}) {
        methods.push_back(automation_method(
            mesh_surface ? "foliage.surface.prepare" : "foliage.terrain.prepare",
            mesh_surface
                ? "Prepare static foliage on the highest upward-facing published LOD0 Mesh "
                  "triangles. "
                  "Use surface (Scene entity UUID) and ordinary mesh.scatter arguments. At most512 "
                  "origins and4096 triangles; selected Mesh limits8MiB/16384vertices/32parts. "
                  "Skin, "
                  "morph, wind, lines/points and near-vertical height projections reject. Uses "
                  "published "
                  "geometry only; publish source edits first. Signed/nonuniform transforms and "
                  "tilted "
                  "surfaces are supported. Every origin needs coverage, including slope-excluded "
                  "ones. "
                  "Optional inclusive0..90degree slopes, height_offset and align_to_slope share "
                  "the "
                  "terrain/scatter owner. Returns arguments, scene_revision, "
                  "surface_mesh/revision/profile; "
                  "no Scene/file changes or live link. Recheck before applying after publication "
                  "changes. "
                  "Use foliage.collision.start for optional clearance, then forge_mesh_scatter and "
                  "Save."
                : "Read a published upright heightfield and prepare static foliage heights. "
                  "Returns "
                  "mesh.scatter arguments and scene_revision; apply them with forge_mesh_scatter "
                  "at that "
                  "revision. No Scene or file changes. On-disk source changes and uncovered "
                  "origins "
                  "reject preparation. Terrain yaw/scaling are supported; tilt/inversion are not. "
                  "Optional minimum_slope/maximum_slope select world geometric triangle slope in "
                  "degrees "
                  "(0..90, inclusive; default0..90). Count is a bounded candidate pool; returned "
                  "placement_mask/retained_count identify the subset, without resampling. Zero "
                  "retained "
                  "candidates rejects. Optional align_to_slope supplies world_normals to the same "
                  "scatter "
                  "owner: tilt the placement frame to the geometric normal; yaw/template rotation "
                  "remain "
                  "relative. Default false retains world-up orientation. No collision tests or "
                  "live link.",
            "asset.read",
            automation_object(
                {{mesh_surface ? "surface" : "terrain", automation_uuid()},
                 {"arguments", scatter_schema},
                 {"height_offset", {{"type", "number"}, {"minimum", -1000}, {"maximum", 1000}}},
                 {"minimum_slope", {{"type", "number"}, {"minimum", 0}, {"maximum", 90}}},
                 {"maximum_slope", {{"type", "number"}, {"minimum", 0}, {"maximum", 90}}},
                 {"align_to_slope", {{"type", "boolean"}}}},
                {mesh_surface ? "surface" : "terrain", "arguments"}),
            false, [&scene, &project, mesh_surface](const Json& request) {
                return (mesh_surface ? prepare_mesh_foliage : prepare_terrain_foliage)(
                    *project.writer_guard(), scene, request.at("arguments"),
                    request.at(mesh_surface ? "surface" : "terrain"),
                    request.value("height_offset", 0.),
                    {request.value("minimum_slope", 0.), request.value("maximum_slope", 90.)},
                    request.value("align_to_slope", false));
            }));
    }
    Json stroke_schema;
    for (const auto& command : authoring_commands())
        if (command.at("id") == "mesh.stroke")
            stroke_schema = command.at("input_schema");
    for (const auto* property : {"world_heights", "world_normals", "placement_mask"})
        stroke_schema["properties"]["dabs"]["items"]["properties"].erase(property);
    methods.push_back(automation_method(
        "foliage.stroke.prepare",
        "Prepare 1 to 16 ordinary foliage dabs, at most 512 total candidates, from one published "
        "surface selection. Every candidate needs upward height coverage. surface_profile is "
        "heightfield or static_mesh; static Mesh samples LOD0 with existing static geometry "
        "limits, heightfield requires upright matching published source. Returns mesh.stroke "
        "arguments and scene_revision for forge_mesh_stroke, one Undo and ordinary saved "
        "objects. No Scene/files change, no live surface link or collision checks. Minimum "
        "spacing applies within each dab only. Recheck after publication changes.",
        "asset.read",
        automation_object(
            {{"surface", automation_uuid()},
             {"surface_profile", {{"type", "string"}, {"enum", {"heightfield", "static_mesh"}}}},
             {"arguments", stroke_schema},
             {"height_offset", {{"type", "number"}, {"minimum", -1000}, {"maximum", 1000}}},
             {"minimum_slope", {{"type", "number"}, {"minimum", 0}, {"maximum", 90}}},
             {"maximum_slope", {{"type", "number"}, {"minimum", 0}, {"maximum", 90}}},
             {"align_to_slope", {{"type", "boolean"}}}},
            {"surface", "surface_profile", "arguments"}),
        false, [&scene, &project](const Json& request) {
            return prepare_foliage_stroke(
                *project.writer_guard(), scene, request.at("arguments"), request.at("surface"),
                request.at("surface_profile") == "static_mesh", request.value("height_offset", 0.),
                {request.value("minimum_slope", 0.), request.value("maximum_slope", 90.)},
                request.value("align_to_slope", false));
        }));
    if (!clearance)
        return methods;
    Json complete_scatter, complete_stroke;
    for (const auto& command : authoring_commands()) {
        if (command.at("id") == "mesh.scatter")
            complete_scatter = command.at("input_schema");
        if (command.at("id") == "mesh.stroke")
            complete_stroke = command.at("input_schema");
    }
    const Json coordinate{{"type", "number"}, {"minimum", -1000}, {"maximum", 1000}};
    Json clearance_profiles = Json::array();
    for (const auto& choice : foliage_clearance_choices())
        clearance_profiles.push_back(choice.key);
    methods.push_back(automation_method(
        "foliage.collision.start",
        "Prepare read-only clearance against current authored native colliders. "
        "Profile world-sphere-v1 (default) uses the supplied radius. Profile mesh-bounds-v1 uses "
        "a conservative world-axis box enclosing the selected static template Mesh at each "
        "saved placement's rotation, signed scale, seeded variation and resolved slope. Every "
        "LOD is enclosed; empty space can be rejected. Bounds and static surface profiles "
        "require disabled template wind; skin/morph sampling is unavailable. "
        "Mesh limits:8MiB data,16384vertices,49152indices, "
        "32parts. Optional padding0..100m expands each box side; sphere radius is inactive. "
        "Profiles world-circle-xy-v1 and mesh-bounds-xy-v1 instead check native XY bodies with "
        "a circle or the transformed Mesh bounds projected to a world-axis XY rectangle. "
        "Profiles mesh-surfaces-v1 and mesh-surfaces-xy-v1 test up to4096 static triangles "
        "from all LODs against native3D or XY colliders. XY drops worldZ and permits segment "
        "or point projections. Surface contact does not test closed-volume containment; a "
        "collider wholly inside a closed template can miss its triangles. Radius and padding "
        "remain validated retained settings but are inactive for surfaces. "
        "Profiles mesh-edit-wind-surfaces-v1 and mesh-edit-wind-surfaces-xy-v1 instead "
        "sample the existing template Wind at the ordinary frozen0.5s Edit pose, then test "
        "its triangles in3D or XY. The shared instance-pose owner supplies wind and bounds. "
        "Offsets shift the sampled geometry after deformation without changing wave phase. "
        "New plants retain ordinary Wind values; no full-cycle or future-motion clearance "
        "is promised. Disabled or zero-amplitude wind uses the static shape. "
        "XY queries and new-candidate separation ignore Z and offset.z. The existing XZ disk, "
        "world heights and saved Mesh transforms still define placement. These are clearance "
        "profiles, not an XY placement or Sprite-template workflow. "
        "Poll foliage.collision.read with the returned operation. Supply ordinary scatter or "
        "stroke arguments, or the result of foliage.terrain.prepare/foliage.surface.prepare/"
        "foliage.stroke.prepare; heights, normals and "
        "original-order masks are "
        "preserved. Sphere radius0.001..100m; XY circle radius0.005..100m. "
        "XY query origins must fit +/-1e6m and rectangle full sizes .01..1000m; "
        "3D retains +/-1e9m origins and .001..10000m box full sizes. "
        "World offset defaults(0,0.5,0)m for all profiles. "
        "Use offset(0,0,0) for unshifted Mesh bounds. Layer mask defaults all; "
        "sensors default excluded. The template and optional surface are ignored automatically, "
        "plus at most14 distinct ignored entity IDs. At most512 candidates,16 shape queries per "
        "poll,8192 per request, including conservative sphere/circle envelope probes and triangle "
        "queries. shape_query_count and shape_query_limit report this request work; "
        "surface_triangle_count reports the source count. "
        "30-second deadline. Exact retries reuse the operation; cancel a different pending request "
        "first. Strokes allow1..16 dabs and512total original candidates; filters also compare "
        "across dabs. Wholly blocked dabs are omitted from returned stroke arguments, with "
        "original indices in retained_dab_indices; all-blocked strokes fail. No simulation or "
        "Scene/file changes. Ready authoring_operation selects forge_mesh_scatter or "
        "forge_mesh_stroke at the returned scene_revision. Task candidate_count includes omitted "
        "dabs. Recheck before applying after published assets/settings change. "
        "Optional avoid_new_overlaps (default false) also filters overlapping new candidate "
        "volumes in original order, keeping the first clear candidate. Surface profiles use "
        "conservative enclosing boxes/XY rectangles for this separation; other profiles use "
        "their selected shape and domain. Touching volumes are allowed. Excluded candidates never "
        "block later ones, no resampling occurs, and a zero collider layer mask still permits "
        "this candidate filter. new_overlap_count reports its exclusions. No project resources "
        "are imported or changed; Mesh bounds use the captured published resource selection.",
        "asset.read",
        automation_object(
            {{"arguments", {{"oneOf", Json::array({complete_scatter, complete_stroke})}}},
             {"profile", {{"type", "string"}, {"enum", clearance_profiles}}},
             {"padding",
              {{"type", "number"},
               {"minimum", 0},
               {"maximum", 100},
               {"description", "Expands Mesh bounds on each side. Inactive retained "
                               "setting for radius and Mesh surface profiles."}}},
             {"radius",
              {{"type", "number"},
               {"minimum", .001},
               {"maximum", 100},
               {"description", "Sphere minimum0.001m; world-circle-xy-v1 minimum0.005m. "
                               "Inactive retained setting for all Mesh profiles."}}},
             {"offset", automation_object({{"x", coordinate}, {"y", coordinate}, {"z", coordinate}},
                                          {"x", "y", "z"})},
             {"mask", automation_integer(0, UINT32_MAX)},
             {"include_sensors", {{"type", "boolean"}}},
             {"avoid_new_overlaps", {{"type", "boolean"}}},
             {"surface", automation_uuid()},
             {"ignored", automation_array(automation_uuid(), 14)}},
            {"arguments"}),
        false, [&scene, &project, clearance](const Json& request) {
            FoliageCollisionSettings settings;
            settings.profile = foliage_clearance_profile(
                request.value("profile", std::string(foliage_clearance_choices().front().key)));
            settings.padding = request.value("padding", settings.padding);
            settings.radius = request.value("radius", settings.radius);
            if (request.contains("offset"))
                settings.offset = {request.at("offset").at("x"), request.at("offset").at("y"),
                                   request.at("offset").at("z")};
            settings.mask = request.value("mask", settings.mask);
            settings.include_sensors = request.value("include_sensors", false);
            settings.avoid_new_overlaps = request.value("avoid_new_overlaps", false);
            if (request.contains("surface"))
                settings.surface = request.at("surface").get<EntityId>();
            if (request.contains("ignored"))
                settings.ignored = request.at("ignored").get<std::vector<EntityId>>();
            return clearance->start(project.writer_guard(), scene, request.at("arguments"),
                                    settings);
        }));
    for (const bool cancel : {false, true})
        methods.push_back(automation_method(
            cancel ? "foliage.collision.cancel" : "foliage.collision.read",
            cancel ? "Cancel this exact transient foliage clearance; authored objects remain "
                     "unchanged."
                   : "Poll/read this exact foliage clearance operation. Ready results contain "
                     "copied scatter arguments and the source Scene revision; "
                     "failures/cancellation have no result.",
            "asset.read", automation_object({{"operation", automation_uuid()}}, {"operation"}),
            false, [&scene, &project, clearance, cancel](const Json& request) {
                project.check_ownership();
                if (clearance->state().value("operation", "") !=
                    request.at("operation").get<std::string>())
                    throw AuthoringError("wrong_operation",
                                         "Foliage clearance operation does not match");
                if (cancel)
                    clearance->cancel();
                return clearance->read(scene, project.project());
            }));
    return methods;
}
} // namespace forge
