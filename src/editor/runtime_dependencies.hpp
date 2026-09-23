#pragma once
#include "../runtime_dependencies.hpp"
#include "../self_executable.hpp"
#include "../ui_asset_catalog.hpp"
#include "../ui_inspection.hpp"
#include "document.hpp"
#include "help.hpp"
#include "property_drawer.hpp"
#include <future>
namespace forge::ui {
// Selected-asset authoring task. Graph edits use the same operation as CLI/export;
// the draft and job status below are transient, not another dependency registry.
class RuntimeDependenciesEditor {
    AssetId owner_;
    std::filesystem::path project_;
    Json expected_;
    std::vector<AssetDependency> edges_;
    Json selected_;
    std::string type_ = "texture", status_, module_;
    std::array<char, 110> reason_{};
    bool dirty_ = false;
    std::future<AssetCatalog> job_;
    void load(const AssetCatalog& catalog, AssetId owner) {
        owner_ = owner;
        const auto persisted = AssetCatalog::open_project(project_);
        expected_ = persisted.document();
        edges_.clear();
        selected_ = nullptr;
        const auto& record = persisted.records().contains(owner) ? persisted.records().at(owner)
                                                                 : catalog.records().at(owner);
        for (const auto& edge : record.dependency_edges)
            if (declared_runtime_edge(edge))
                edges_.push_back(edge);
        dirty_ = false;
        status_ = "Review conditional UI and gameplay-selected content here.";
    }

  public:
    bool busy() const { return job_.valid(); }
    bool dirty() const { return dirty_; }
    void reveal_draft() const {
        if (editor_context && dirty_) {
            editor_context->selection.select_asset(owner_);
            editor_context->reveal_content = true;
        }
    }
    void poll(const std::function<void()>& refresh) {
        if (job_.valid() && job_.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
            try {
                auto result = job_.get();
                load(result, owner_);
                status_ = "Dependencies saved and source revisions reviewed.";
                if (editor_context)
                    editor_context->problems.resolve("runtime-dependencies:" + owner_.str());
                refresh();
            } catch (const std::exception& e) {
                status_ = e.what();
                report_error("runtime-dependencies:" + owner_.str(), status_);
            }
        }
    }
    void draw(SceneDocument& document, const AssetCatalog& catalog, const AssetRecord& asset,
              bool locked, const std::function<void(const AssetRecord&)>& open) {
        project_ = document.project();
        const bool expanded = responsive_tree_node("Runtime Dependencies", "Dependencies");
        FORGE_UI_PROBE("dependencies:section");
        if (!expanded) {
            help("Declare finite conditional content needed by this asset or its gameplay. "
                 "Ordinary reflected references are automatic.");
            return;
        }
        help("All declared dependencies are required by export. These project catalog edits have "
             "their own Save and are separate from scene Undo.");
        if (owner_ != asset.id && dirty_ && !busy()) {
            ImGui::TextWrapped("Unsaved dependency draft for the previous asset.");
            if (button("Return to draft", "Select the owner of the unsaved dependency draft.",
                       "Return"))
                editor_context->selection.select_asset(owner_);
            if (button("Discard draft and inspect selection",
                       "Discard unsaved dependency changes only.", "Discard"))
                load(catalog, asset.id);
            ImGui::TreePop();
            return;
        }
        if (owner_ != asset.id && !busy())
            load(catalog, asset.id);
        if (owner_ != asset.id) {
            ImGui::TextWrapped("Finishing dependency operation for the previous selection.");
            ImGui::TreePop();
            return;
        }
        ImGui::TextWrapped("%s", status_.c_str());
        help("Export validates reviewed source hashes again. Source changes require reviewing and "
             "saving declarations again.");
        ImGui::BeginDisabled(locked || busy() || asset.subasset.has_value());
        auto show = [&](const AssetDependency& edge) {
            const auto resolution = catalog.resolve(edge.target, edge.expected_type);
            ImGui::TextWrapped("%s · %s", edge.expected_type.c_str(),
                               resolution.record ? asset_display(*resolution.record).c_str()
                                                 : "Missing asset");
            help(edge.role.c_str());
            if (resolution.state != AssetState::Available)
                field_error(resolution.diagnostic);
            if (resolution.record) {
                if (button("Reveal", "Select this dependency in Content.")) {
                    editor_context->selection.select_asset(edge.target);
                    editor_context->reveal_content = true;
                }
                next_text_button("Open");
                if (button("Open", "Open this dependency through its registered asset editor.")) {
                    try {
                        open(*resolution.record);
                    } catch (const std::exception& e) {
                        status_ = e.what();
                        report_error("runtime-dependencies:" + owner_.str(), status_);
                    }
                }
            }
        };
        if (responsive_tree_node("Automatic dependencies", "Automatic")) {
            help("Recorded static dependencies. Export also refreshes reflected scene/prefab "
                 "references and native UI resources.");
            for (const auto& edge : asset.dependency_edges)
                if (edge.kind == AssetDependencyKind::Runtime && !declared_runtime_edge(edge) &&
                    edge.role != "ui.observed") {
                    IdScope id((edge.target.str() + edge.role).c_str());
                    show(edge);
                }
            ImGui::TreePop();
        }
        if (responsive_tree_node("Observed resources", "Observed")) {
            help("Preview observations are advisory. Confirm resources needed in conditional "
                 "states to include them in export.");
            for (const auto& edge : asset.dependency_edges)
                if (edge.role == "ui.observed") {
                    IdScope id((edge.target.str() + edge.role).c_str());
                    show(edge);
                    if (button("Add to Runtime Dependencies",
                               "Explicitly declare this observed resource; save the draft to "
                               "publish.",
                               "Declare")) {
                        auto value = edge;
                        value.kind = AssetDependencyKind::Runtime;
                        value.role = "declared:UI state";
                        value.revision.clear();
                        if (std::find(edges_.begin(), edges_.end(), value) == edges_.end())
                            edges_.push_back(value);
                        dirty_ = true;
                    }
                }
            ImGui::TreePop();
        }
        ImGui::SeparatorText(ImGui::CalcTextSize("Declared runtime dependencies").x >
                                     ImGui::GetContentRegionAvail().x
                                 ? "Declared"
                                 : "Declared runtime dependencies");
        help("Required finite set. Scene-owned declarations may cover gameplay-selected scenes, "
             "skins and other module content.");
        for (std::size_t i = 0; i < edges_.size();) {
            IdScope id(std::to_string(i).c_str());
            show(edges_[i]);
            if (button("Remove", "Remove this declaration from the draft. Save to apply.")) {
                edges_.erase(edges_.begin() + std::ptrdiff_t(i));
                dirty_ = true;
            } else
                ++i;
        }
        std::set<std::string> types;
        for (const auto& [id, r] : catalog.records())
            types.insert(r.type);
        ImGui::TextWrapped("Asset type");
        help("Expected type for the runtime dependency.");
        ImGui::SetNextItemWidth(-FLT_MIN);
        if (ImGui::BeginCombo("##dependency-type", type_.c_str())) {
            for (const auto& value : types) {
                if (ImGui::Selectable(value.c_str(), value == type_)) {
                    type_ = value;
                    selected_ = nullptr;
                }
                FORGE_UI_PROBE("dependencies:type:" + value);
                help("Required asset type; mismatches are rejected before saving.");
            }
            ImGui::EndCombo();
        }
        FORGE_UI_PROBE("dependencies:type");
        help("Choose the expected asset type, then search or drag an asset from Content.");
        ImGui::TextWrapped("Resource");
        help("Search registered or discovered assets, or drop one from Content.");
        ImGui::SetNextItemWidth(-FLT_MIN);
        asset_ref_picker(catalog, selected_, type_, "##dependency-resource", false);
        ImGui::TextWrapped("Selection owner");
        help("The document or gameplay module that selects this content at runtime.");
        ImGui::SetNextItemWidth(-FLT_MIN);
        if (ImGui::BeginCombo("##dependency-owner",
                              module_.empty() ? "This asset / document" : module_.c_str())) {
            if (ImGui::Selectable("This asset / document", module_.empty()))
                module_.clear();
            for (const auto& module :
                 document.settings().document().value("modules", Json::array()))
                if (module.is_object()) {
                    const auto name = module.at("id").get<std::string>();
                    if (ImGui::Selectable(name.c_str(), module_ == name))
                        module_ = name;
                }
            ImGui::EndCombo();
        }
        help("Native-module selections stay on this asset's graph. Export requires this owner to "
             "be reachable; changing the module build requires reviewing its declarations again.");
        ImGui::TextWrapped("Reason / group");
        help("Describe why runtime selection needs this asset.");
        ImGui::SetNextItemWidth(-FLT_MIN);
        ImGui::InputTextWithHint("##dependency-reason", "Hover images or character skins",
                                 reason_.data(), reason_.size());
        FORGE_UI_PROBE("dependencies:reason");
        help("Explain the runtime selection that needs this finite resource. This reason stays on "
             "the authoritative graph edge.");
        ImGui::BeginDisabled(selected_.is_null() || !reason_[0]);
        if (button("Add dependency", "Add a typed required dependency to the draft.", "Add")) {
            AssetDependency edge{
                selected_.get<AssetId>(),
                type_,
                AssetDependencyKind::Runtime,
                "declared:" + (module_.empty() ? std::string() : "module/" + module_ + "/") +
                    std::string(reason_.data()),
                {}};
            if (std::find(edges_.begin(), edges_.end(), edge) == edges_.end())
                edges_.push_back(edge);
            dirty_ = true;
            selected_ = nullptr;
        }
        FORGE_UI_PROBE("dependencies:add");
        ImGui::EndDisabled();
        if (button(dirty_ ? "Save declarations *" : "Review / save declarations",
                   "Refresh native UI static dependencies, validate types and reviewed source "
                   "revisions, then atomically publish the catalog. Scene Undo is unchanged.",
                   dirty_ ? "Save *" : "Review")) {
            try {
                auto lease = document.writer_guard();
                auto expected = expected_;
                auto edges = edges_;
                auto owner = owner_;
                auto kind = asset.type;
                std::vector<AssetRecord> discoveries;
                std::set<AssetId> requested{owner};
                for (const auto& edge : edges)
                    requested.insert(edge.target);
                for (const auto id : requested)
                    if (const auto found = catalog.records().find(id);
                        found != catalog.records().end() &&
                        (found->second.type == SceneAsset::type ||
                         found->second.type == PrefabAsset::type))
                        discoveries.push_back(found->second);
                job_ = std::async(std::launch::async, [lease, expected, edges, owner, kind,
                                                       discoveries] {
                    if (AssetCatalog::open_project(lease->root()).document() != expected)
                        throw std::runtime_error("Catalog changed. Discard this draft and review "
                                                 "the new dependency list.");
                    std::optional<UiAssetSnapshot> snapshot;
                    if (kind == UiDocumentAsset::type) {
                        auto executable = self_executable().parent_path() / "forge_ui_inspect";
#ifdef _WIN32
                        executable += ".exe";
#endif
                        snapshot = inspect_ui_dependencies(executable,
                                                           self_executable().parent_path() /
                                                               "resources/ui/LatoLatin-Regular.ttf",
                                                           lease->root(), owner);
                    }
                    return declare_runtime_dependencies(*lease, owner, edges, expected,
                                                        snapshot ? &*snapshot : nullptr,
                                                        discoveries);
                });
                status_ = "Validating and saving dependencies...";
            } catch (const std::exception& e) {
                status_ = e.what();
                report_error("runtime-dependencies:" + owner_.str(), status_);
            }
        }
        FORGE_UI_PROBE("dependencies:save");
        if (button("Discard draft / refresh",
                   "Reload saved declarations and current catalog "
                   "status without changing project data.",
                   "Discard"))
            load(catalog, asset.id);
        FORGE_UI_PROBE("dependencies:discard");
        ImGui::EndDisabled();
        ImGui::TreePop();
    }
};
} // namespace forge::ui
