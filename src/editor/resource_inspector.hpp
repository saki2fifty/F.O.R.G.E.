#pragma once
#include "../mesh_render_host.hpp"
#include "asset_labels.hpp"
#include "widgets.hpp"
#include <forge/engine_assets.hpp>
namespace forge::ui {
class ResourceInspector {
  public:
    bool visible = false;
    std::function<void(AssetId)> reveal;
    void menu() {
        ImGui::MenuItem("Loaded resources", nullptr, &visible);
        help("Inspect loaded scene resources, failed replacements, leases and CPU/GPU payloads.");
    }
    void draw(const std::shared_ptr<MeshResourceHost>& host, DiligentPresentation& presentation) {
        if (!visible)
            return;
        draft_window_size({850 * interface_scale, 680 * interface_scale});
        if (ImGui::Begin("Loaded resources", &visible)) {
            heading("Scene resources", "Main editor Scene/Game resource owner. Independent asset "
                                       "preview owners and the runtime process are not included.");
            if (!host) {
                ImGui::TextUnformatted("No active scene resource owner.");
                help("Open a project and display scene content to load resources.");
            } else {
                const auto now = std::chrono::steady_clock::now();
                if (now >= refresh_ || previous_.lock() != host) {
                    snapshot_ = host->inspect();
                    previous_ = host;
                    refresh_ = now + std::chrono::milliseconds(500);
                }
                auto cpu = [&](const char* name, const ResourceStatistics& value) {
                    ImGui::TextWrapped("%s: %zu loaded, %zu retiring, %zu pending | %.2f MiB", name,
                                       value.selected, value.retired, value.pending,
                                       double(value.memory.total()) / (1024 * 1024));
                    help("Owned CPU payload, including retained old revisions. This excludes "
                         "allocator overhead and other processes.");
                };
                cpu("Meshes", snapshot_.meshes);
                cpu("Materials", snapshot_.materials);
                cpu("Textures", snapshot_.textures);
                auto gpu = [&](const char* name, const GpuResidencyStats& value) {
                    ImGui::TextWrapped("%s: %zu resident, %zu retiring | %.2f MiB", name,
                                       value.resident, value.retiring,
                                       double(value.payload_bytes) / (1024 * 1024));
                    help("Requested GPU payload includes in-flight retirement. Driver allocation "
                         "padding, descriptor heaps, frame targets and pipeline overhead are not "
                         "included; this is not total VRAM usage.");
                };
                gpu("GPU mesh buffers", snapshot_.gpu_meshes);
                gpu("GPU textures", snapshot_.gpu_textures);
                gpu("GPU environments", snapshot_.environments);
                ImGui::TextWrapped("Presentation shader / pipeline cache: %llu hits, %llu misses",
                                   static_cast<unsigned long long>(presentation.cache_hits()),
                                   static_cast<unsigned long long>(presentation.cache_misses()));
                help("Device-wide Diligent cache queries. Native pipeline allocation sizes are "
                     "not exposed as portable byte accounting.");
                if (button("Refresh", "Refresh the owner-thread diagnostic snapshot now."))
                    refresh_ = {};
                ImGui::Separator();
                heading("Loaded revisions", "Strong leases pin a loaded revision. Retiring "
                                            "revisions remain until users release them.");
                const auto catalog = host->catalog();
                rows(snapshot_.revisions, catalog.get(), true);
                heading("Requests and errors", "Failed replacement requests retain their last "
                                               "usable revision when one exists.");
                rows(snapshot_.requests, catalog.get(), false);
            }
        }
        ImGui::End();
    }

  private:
    void rows(const std::vector<ResourceInfo>& rows, const AssetCatalog* catalog, bool loaded) {
        if (rows.empty()) {
            ImGui::TextDisabled("None");
            help("No resources are currently recorded in this group.");
            return;
        }
        for (std::size_t i = 0; i < rows.size(); ++i) {
            const auto& row = rows[i];
            std::string name = row.identity.type;
            const AssetRecord* record = nullptr;
            if (catalog) {
                const auto found = catalog->records().find(row.identity.asset);
                if (found != catalog->records().end()) {
                    record = &found->second;
                    name = content_member_name(*record);
                    if (name.empty())
                        name = path_utf8(record->source.filename());
                }
            }
            if (const auto* engine = engine_asset(row.identity.asset))
                name = engine->name;
            ImGui::PushID(loaded ? "loaded" : "requests");
            ImGui::PushID(int(i));
            const auto label = name + " — " + resource_state_name(row.state);
            const bool open = ImGui::TreeNodeEx("resource", 0, "%s", label.c_str());
            help("Expand to inspect this process-local revision. AssetId remains the durable "
                 "logical identity; loading and reimport do not allocate a new one.");
            if (open) {
                ImGui::TextWrapped("AssetId: %s", row.identity.asset.str().c_str());
                help("Persistent logical asset identity.");
                ImGui::TextWrapped("Revision: %s", row.identity.revision.c_str());
                help("Immutable revision selected by this resource request.");
                if (record) {
                    ImGui::TextWrapped("Source: %s", path_utf8(record->source).c_str());
                    help("Project-relative source locator from the current catalog.");
                    if (reveal &&
                        button("Show in Content", "Select this logical asset in Content "
                                                  "and inspect its import/dependency data."))
                        reveal(row.identity.asset);
                }
                if (loaded) {
                    ImGui::TextWrapped("Strong leases: %zu | CPU payload: %.2f MiB",
                                       row.strong_leases,
                                       double(row.memory.total()) / (1024 * 1024));
                    help("Lease references held outside the pool at snapshot time. They prevent "
                         "eviction of this immutable revision.");
                }
                if (!row.identity.variant.empty()) {
                    ImGui::TextWrapped("Variant: %s", row.identity.variant.c_str());
                    help("Semantic load variant, such as a texture's transfer/usage "
                         "interpretation.");
                }
                if (row.previous_good) {
                    ImGui::TextUnformatted("Previous good revision retained");
                    help("This failed or pending request has not replaced its usable resource.");
                }
                if (!row.diagnostic.empty())
                    field_error(row.diagnostic);
                ImGui::TreePop();
            }
            ImGui::PopID();
            ImGui::PopID();
        }
    }
    MeshResourceInspection snapshot_;
    std::weak_ptr<MeshResourceHost> previous_;
    std::chrono::steady_clock::time_point refresh_{};
};
} // namespace forge::ui
