#pragma once
#include "content_model.hpp"
#include "editor_state.hpp"
#include "icons.hpp"
#include <cmath>
#include <functional>
#include <utility>
namespace forge {
// ImGui adapter over immutable discovery data. All writes remain in application services.
struct ContentThumbnail {
    ImTextureID image = 0;
    float aspect = 1;
    std::string status;
};
class ContentView {
  public:
    std::function<void(const ContentEntry&)> open, context_menu;
    std::function<ContentThumbnail(AssetId)> thumbnail;
    std::function<void(AssetId)> retry_thumbnail;
    std::function<void(const std::vector<AssetId>&)> reimport;
    Json settings() const {
        return {{"grid", grid_},
                {"folder_tree", tree_},
                {"tile_size", tile_size_},
                {"descendants", query_.descendants}};
    }
    void load_settings(const Json& value) {
        if (!value.is_object())
            return;
        if (value.contains("grid") && value.at("grid").is_boolean())
            grid_ = value.at("grid");
        if (value.contains("folder_tree") && value.at("folder_tree").is_boolean())
            tree_ = value.at("folder_tree");
        if (value.contains("descendants") && value.at("descendants").is_boolean())
            query_.descendants = value.at("descendants");
        if (value.contains("tile_size") && value.at("tile_size").is_number()) {
            const auto size = value.at("tile_size").get<float>();
            if (std::isfinite(size))
                tile_size_ = std::clamp(size, 80.f, 220.f);
        }
    }
    bool take_settings_changed() { return std::exchange(settings_changed_, false); }
    void update(std::shared_ptr<const ContentIndex> index) {
        if (index_ == index)
            return;
        auto next = index ? index->query(query_) : std::vector<std::size_t>{};
        bool order_changed = next.size() != visible_.size() || !index_;
        if (!order_changed)
            for (std::size_t i = 0; i < next.size(); ++i)
                if (index_->entries[visible_[i]].key != index->entries[next[i]].key) {
                    order_changed = true;
                    break;
                }
        index_ = std::move(index);
        if (index_) {
            std::erase_if(selected_, [&](const auto& key) { return !index_->keys.contains(key); });
        } else
            selected_.clear();
        visible_ = std::move(next);
        applied_ = query_;
        dirty_ = false;
        if (order_changed)
            ++scope_;
    }
    void reveal(const ui::EditorSelection& selection) {
        if (!index_)
            return;
        for (const auto& entry : index_->entries)
            if (matches(entry, selection)) {
                locations_.visit(path_utf8(entry.source.parent_path()));
                query_ = {};
                search_[0] = 0;
                selected_ = {entry.key};
                reveal_key_ = entry.key;
                dirty_ = true;
                break;
            }
    }
    const std::set<std::string>& selection() const { return selected_; }
    std::vector<AssetId> selected_assets() const {
        std::vector<AssetId> result;
        if (index_)
            for (const auto& entry : index_->entries)
                if (entry.asset && selected_.contains(entry.key))
                    result.push_back(entry.asset);
        return result;
    }
    void draw(ui::EditorSelection& selection, bool locked) {
        const float available = ImGui::GetContentRegionAvail().x;
        const bool wide = available >= 700 * ui::interface_scale;
        ImGui::SetNextItemWidth(wide ? available * .46f : -1);
        ImGui::InputTextWithHint("##asset-search", "Search name, path, type, status...", search_,
                                 sizeof(search_));
        ui::help("Case-insensitive words match together across display name, source path, "
                 "extension, type and status.");
        query_.text = search_;
        if (wide)
            ImGui::SameLine();
        const float filter_width =
            std::max(65.f * ui::interface_scale,
                     ((wide ? available * .54f : available) - 90 * ui::interface_scale) * .5f);
        ImGui::SetNextItemWidth(filter_width);
        if (ImGui::BeginCombo("##type", query_.type.empty() ? "All types" : query_.type.c_str())) {
            if (ImGui::Selectable("All types", query_.type.empty()))
                query_.type.clear();
            if (index_)
                for (const auto& type : index_->types)
                    if (ImGui::Selectable(type.c_str(), query_.type == type))
                        query_.type = type;
            ImGui::EndCombo();
        }
        ui::help("Filter by the logical asset type or recognized source-file type.");
        ImGui::SameLine();
        ImGui::SetNextItemWidth(filter_width);
        if (ImGui::BeginCombo("##state",
                              query_.state ? content_state_label(*query_.state) : "All states")) {
            if (ImGui::Selectable("All states", !query_.state))
                query_.state.reset();
            for (auto state :
                 {ContentState::Registered, ContentState::Published, ContentState::Unimported,
                  ContentState::Changed, ContentState::Missing, ContentState::Removed,
                  ContentState::Queued, ContentState::Importing, ContentState::Failed})
                if (ImGui::Selectable(content_state_label(state), query_.state == state))
                    query_.state = state;
            ImGui::EndCombo();
        }
        ui::help("Discovery status from the latest complete source scan. Published records are not "
                 "a claim that a GPU resource is loaded; Source updates shows import failures.");
        ui::next_text_button("View");
        if (ui::button("View", "Choose list or grid, tile size, and folder visibility."))
            ImGui::OpenPopup("content-view");
        if (ImGui::BeginPopup("content-view")) {
            const auto before = settings();
            if (ImGui::RadioButton("List", !grid_))
                grid_ = false;
            ui::help("Compact asset names, logical types and source states.");
            if (ImGui::RadioButton("Grid", grid_))
                grid_ = true;
            ui::help("Asset tiles with names and types. A type icon is a fallback, not a rendered "
                     "preview.");
            ImGui::SliderFloat("Tile size", &tile_size_, 80, 220, "%.0f px");
            ui::help("Logical tile width before interface zoom. Content wraps to available space.");
            ImGui::Checkbox("Folder tree", &tree_);
            ui::help("Show project source folders beside the asset results. Narrow Content panels "
                     "use the folder menu.");
            ImGui::Checkbox("Include subfolders", &query_.descendants);
            ui::help("Search the selected folder and descendants; turn off to see only files "
                     "directly in that folder.");
            settings_changed_ |= before != settings();
            ImGui::EndPopup();
        }
        ImGui::BeginDisabled(!locations_.back_available());
        if (ui::button("<", "Back to the previous Content folder."))
            locations_.back();
        ImGui::EndDisabled();
        ImGui::SameLine();
        ImGui::BeginDisabled(!locations_.forward_available());
        if (ui::button(">", "Forward to the next Content folder."))
            locations_.forward();
        ImGui::EndDisabled();
        ui::next_text_button("Project");
        if (ui::button("Project", "Browse all project content from the root folder."))
            locations_.visit("");
        std::filesystem::path breadcrumb;
        const auto location =
            locations_.current(); // Clicking a crumb must not mutate iteration storage.
        for (const auto& part : std::filesystem::u8path(location)) {
            breadcrumb /= part;
            const auto label = path_utf8(part);
            ui::next_text_button(label.c_str());
            ui::IdScope id(path_utf8(breadcrumb).c_str());
            if (ui::button(label.c_str(), "Browse this ancestor folder."))
                locations_.visit(path_utf8(breadcrumb));
        }
        ui::next_text_button("Folders");
        if (ui::button("Folders", "Choose a project folder, including at narrow panel widths."))
            ImGui::OpenPopup("folders");
        if (ImGui::BeginPopup("folders")) {
            draw_folder("", 0);
            ImGui::EndPopup();
        }
        ImGui::SameLine();
        ImGui::TextDisabled("%zu selected", selected_.size());
        ui::help("Ctrl-click toggles items; Shift-click selects a range; Ctrl+A selects filtered "
                 "results; Escape clears Content selection. Inspector follows the primary item. "
                 "Hidden selected items remain selected until cleared.");
        if (reimport && !selected_.empty()) {
            ui::next_text_button("Reimport selected");
            ImGui::BeginDisabled(locked);
            if (ui::button(
                    "Reimport selected",
                    "Queue selected registered assets through their existing importer. Shared "
                    "source owners are processed once; dirty source drafts delay publication.")) {
                const auto assets = selected_assets();
                if (assets.size() != selected_.size())
                    ui::report_error("content_reimport",
                                     "Import selected source files before requesting Reimport "
                                     "selected. No assets were queued.");
                else
                    reimport(assets);
            }
            ImGui::EndDisabled();
        }
        query_.folder = locations_.current();
        if (dirty_ || query_ != applied_) {
            visible_ = index_ ? index_->query(query_) : std::vector<std::size_t>{};
            applied_ = query_;
            dirty_ = false;
            ++scope_;
        }
        const auto height = std::max(40.f, ImGui::GetContentRegionAvail().y -
                                               ImGui::GetTextLineHeightWithSpacing());
        if (tree_ && ImGui::GetContentRegionAvail().x > 580 * ui::interface_scale) {
            ImGui::BeginChild("content-folders", {170 * ui::interface_scale, height},
                              ImGuiChildFlags_Borders);
            draw_folder("", 0);
            ImGui::EndChild();
            ImGui::SameLine();
        }
        ImGui::BeginChild("content-results", {0, height}, ImGuiChildFlags_Borders);
        if (!index_ || visible_.empty())
            ImGui::TextWrapped(index_
                                   ? "No matching assets. Clear filters or choose another folder."
                                   : "Discovering project content...");
        else
            draw_entries(selection, locked);
        ImGui::EndChild();
        ImGui::TextDisabled("%zu shown / %zu total", visible_.size(),
                            index_ ? index_->entries.size() : 0);
        ui::help("Results use the latest complete background index. Refresh and source changes "
                 "replace the index without assigning asset identities.");
    }

  private:
    std::shared_ptr<const ContentIndex> index_;
    ContentLocations locations_;
    ContentQuery query_, applied_;
    std::vector<std::size_t> visible_;
    std::set<std::string> selected_;
    std::string reveal_key_;
    char search_[256]{};
    bool dirty_ = true, grid_ = false, tree_ = true, settings_changed_ = false;
    float tile_size_ = 120;
    unsigned scope_ = 0;
    static bool matches(const ContentEntry& entry, const ui::EditorSelection& selection) {
        return entry.asset ? selection.kind() == ui::SelectionKind::Asset &&
                                 selection.asset() == entry.asset
                           : selection.kind() == ui::SelectionKind::DocumentItem &&
                                 selection.document() == "content.source" &&
                                 selection.member() == entry.path;
    }
    static void primary(const ContentEntry& entry, ui::EditorSelection& selection) {
        if (entry.asset)
            selection.select_asset(entry.asset);
        else {
            selection.select_document_item("content.source", entry.path);
            if (ui::editor_context)
                ui::editor_context->task.focus_document("content.source", "Source file");
        }
    }
    void draw_folder(const std::string& path, unsigned depth) {
        if (!index_ || depth > 64)
            return;
        const auto found = index_->folders.find(path);
        if (found == index_->folders.end())
            return;
        ui::IdScope id(path.c_str());
        auto flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_SpanAvailWidth;
        if (path == locations_.current())
            flags |= ImGuiTreeNodeFlags_Selected;
        if (found->second.empty())
            flags |= ImGuiTreeNodeFlags_Leaf;
        if (path.empty())
            flags |= ImGuiTreeNodeFlags_DefaultOpen;
        const auto label =
            path.empty() ? "Project" : path_utf8(std::filesystem::u8path(path).filename());
        const bool expanded = ImGui::TreeNodeEx("folder", flags, "%s", label.c_str());
        if (ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen())
            locations_.visit(path);
        ui::help(path.empty() ? "All project source folders." : path.c_str());
        if (expanded) {
            for (const auto& child : found->second)
                draw_folder(child, depth + 1);
            ImGui::TreePop();
        }
    }
    void apply(ImGuiMultiSelectIO* io, ui::EditorSelection& selection) {
        const ContentEntry* last = nullptr;
        for (const auto& request : io->Requests) {
            if (request.Type == ImGuiSelectionRequestType_SetAll) {
                if (!request.Selected)
                    selected_.clear();
                else
                    for (const auto i : visible_)
                        selected_.insert(index_->entries[i].key);
            } else if (request.Type == ImGuiSelectionRequestType_SetRange) {
                const auto first = std::min(request.RangeFirstItem, request.RangeLastItem);
                const auto end = std::max(request.RangeFirstItem, request.RangeLastItem);
                if (first < 0 || end >= static_cast<ImS64>(visible_.size()))
                    continue;
                for (auto i = first; i <= end; ++i) {
                    const auto& entry = index_->entries[visible_[static_cast<std::size_t>(i)]];
                    if (request.Selected)
                        selected_.insert(entry.key);
                    else
                        selected_.erase(entry.key);
                }
                if (request.Selected)
                    last =
                        &index_->entries[visible_[static_cast<std::size_t>(request.RangeLastItem)]];
            }
        }
        if (last)
            primary(*last, selection);
        else if (!io->Requests.empty()) {
            const ContentEntry* fallback = nullptr;
            bool primary_selected = false;
            for (const auto& entry : index_->entries)
                if (selected_.contains(entry.key)) {
                    if (!fallback)
                        fallback = &entry;
                    primary_selected |= matches(entry, selection);
                }
            if (!primary_selected && fallback)
                primary(*fallback, selection);
            else if (!fallback && (selection.kind() == ui::SelectionKind::Asset ||
                                   (selection.kind() == ui::SelectionKind::DocumentItem &&
                                    selection.document() == "content.source")))
                selection.clear();
        }
    }
    void draw_entries(ui::EditorSelection& selection, bool locked) {
        ImGui::PushID(static_cast<int>(
            scope_)); // Reset ImGui's row-range anchor when the projection changes.
        auto* io = ImGui::BeginMultiSelect(
            ImGuiMultiSelectFlags_ClearOnEscape | ImGuiMultiSelectFlags_ClearOnClickVoid |
                ImGuiMultiSelectFlags_NavWrapX | ImGuiMultiSelectFlags_SelectOnClickRelease,
            static_cast<int>(selected_.size()), static_cast<int>(visible_.size()));
        apply(io, selection);
        const float width = ImGui::GetContentRegionAvail().x;
        const float gap = ImGui::GetStyle().ItemSpacing.x;
        const int columns =
            grid_ ? std::max(1, static_cast<int>((width + gap) /
                                                 (tile_size_ * ui::interface_scale + gap)))
                  : 1;
        const float item_width = std::max(1.f, (width - gap * (columns - 1)) / columns);
        const float item_height =
            grid_ ? tile_size_ * ui::interface_scale + 2 * ImGui::GetTextLineHeightWithSpacing()
                  : ImGui::GetTextLineHeightWithSpacing();
        const float stride = item_height + ImGui::GetStyle().ItemSpacing.y;
        const auto origin = ImGui::GetCursorScreenPos();
        ImGuiListClipper clipper;
        clipper.Begin(static_cast<int>((visible_.size() + columns - 1) / columns), stride);
        if (io->RangeSrcItem >= 0 && io->RangeSrcItem < static_cast<ImS64>(visible_.size()))
            clipper.IncludeItemByIndex(static_cast<int>(io->RangeSrcItem) / columns);
        for (std::size_t i = 0; !reveal_key_.empty() && i < visible_.size(); ++i)
            if (index_->entries[visible_[i]].key == reveal_key_)
                clipper.IncludeItemByIndex(static_cast<int>(i) / columns);
        while (clipper.Step()) {
            for (int row = clipper.DisplayStart; row < clipper.DisplayEnd; ++row) {
                for (int column = 0; column < columns; ++column) {
                    const auto i = static_cast<std::size_t>(row * columns + column);
                    if (i >= visible_.size())
                        break;
                    const auto& entry = index_->entries[visible_[i]];
                    ui::IdScope id(entry.key.c_str());
                    const ImVec2 pos{origin.x + column * (item_width + gap),
                                     origin.y + row * stride};
                    ImGui::SetCursorScreenPos(pos);
                    ImGui::SetNextItemSelectionUserData(static_cast<ImS64>(i));
                    const bool selected = selected_.contains(entry.key);
                    if (ImGui::Selectable("##asset", selected,
                                          ImGuiSelectableFlags_AllowDoubleClick,
                                          {item_width, item_height})) {
                        if (!locked && ImGui::IsMouseDoubleClicked(0) && open)
                            open(entry);
                    }
                    if (ImGui::IsItemClicked(ImGuiMouseButton_Right))
                        primary(entry, selection);
                    const auto preview = grid_ && entry.asset && thumbnail && ImGui::IsItemVisible()
                                             ? thumbnail(entry.asset)
                                             : ContentThumbnail{};
                    ui::help((entry.name + "\n" + entry.path + "\n" + entry.type + " | " +
                              content_state_label(entry.state) +
                              "\nCtrl/Shift: multiple selection. Double-click: open. Right-click: "
                              "actions." +
                              (preview.status.empty() ? "" : "\n" + preview.status))
                                 .c_str());
                    if (reveal_key_ == entry.key) {
                        ImGui::SetScrollHereY();
                        reveal_key_.clear();
                    }
                    if (entry.asset && ImGui::BeginDragDropSource()) {
                        const auto payload = entry.asset.str();
                        ImGui::SetDragDropPayload("FORGE_ASSET", payload.c_str(),
                                                  payload.size() + 1);
                        ImGui::Text("%s (%s)", entry.name.c_str(), entry.type.c_str());
                        if (selected_.size() > 1)
                            ImGui::TextUnformatted("Dragging this asset only");
                        ImGui::EndDragDropSource();
                    }
                    if (ImGui::BeginPopupContextItem("asset-actions")) {
                        if (ImGui::MenuItem("Inspect"))
                            primary(entry, selection);
                        if (entry.asset && ImGui::MenuItem("Copy AssetId"))
                            ImGui::SetClipboardText(entry.asset.str().c_str());
                        if (ImGui::MenuItem("Copy source path"))
                            ImGui::SetClipboardText(entry.path.c_str());
                        if (entry.asset && retry_thumbnail && !preview.status.empty() &&
                            ImGui::MenuItem("Refresh thumbnail"))
                            retry_thumbnail(entry.asset);
                        if (context_menu)
                            context_menu(entry);
                        ImGui::EndPopup();
                    }
                    auto* draw = ImGui::GetWindowDrawList();
                    draw->PushClipRect(pos, {pos.x + item_width, pos.y + item_height}, true);
                    const auto color = ImGui::GetColorU32(ImGuiCol_Text);
                    const auto muted = ImGui::GetColorU32(ImGuiCol_TextDisabled);
                    if (grid_) {
                        const float icon =
                            std::min(item_width * .4f, tile_size_ * ui::interface_scale * .45f);
                        const ImVec2 lo{pos.x + (item_width - icon) * .5f,
                                        pos.y + 12 * ui::interface_scale};
                        if (preview.image) {
                            const float box_w = std::max(1.f, item_width - 8 * ui::interface_scale);
                            const float box_h = std::max(
                                1.f, item_height - 2 * ImGui::GetTextLineHeightWithSpacing() -
                                         8 * ui::interface_scale);
                            const float h = std::min(box_h, box_w / preview.aspect);
                            const float w = h * preview.aspect;
                            const ImVec2 a{pos.x + (item_width - w) * .5f,
                                           pos.y + (box_h - h) * .5f + 4 * ui::interface_scale};
                            draw->AddImage(ImTextureRef{preview.image}, a, {a.x + w, a.y + h});
                        } else {
                            draw->AddRect(lo, {lo.x + icon, lo.y + icon}, muted,
                                          4 * ui::interface_scale, 0, 1.5f * ui::interface_scale);
                            draw->AddLine({lo.x + icon * .2f, lo.y + icon * .4f},
                                          {lo.x + icon * .8f, lo.y + icon * .4f}, muted);
                        }
                        draw->AddText({pos.x + 4, pos.y + item_height -
                                                      2 * ImGui::GetTextLineHeightWithSpacing()},
                                      color, entry.name.c_str());
                        draw->AddText({pos.x + 4,
                                       pos.y + item_height - ImGui::GetTextLineHeightWithSpacing()},
                                      muted, entry.type.c_str());
                    } else {
                        const float name_end = width * .58f;
                        draw->PushClipRect(pos, {pos.x + name_end - 5, pos.y + item_height}, true);
                        draw->AddText(pos, color, entry.name.c_str());
                        draw->PopClipRect();
                        draw->PushClipRect({pos.x + name_end, pos.y},
                                           {pos.x + width * .78f - 5, pos.y + item_height}, true);
                        draw->AddText({pos.x + name_end, pos.y}, muted, entry.type.c_str());
                        draw->PopClipRect();
                        draw->AddText({pos.x + width * .78f, pos.y}, muted,
                                      content_state_label(entry.state));
                    }
                    draw->PopClipRect();
                }
            }
        }
        apply(ImGui::EndMultiSelect(), selection);
        ImGui::PopID();
    }
};
} // namespace forge
