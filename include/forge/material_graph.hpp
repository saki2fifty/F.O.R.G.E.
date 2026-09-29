#pragma once
#include <forge/identity.hpp>
#include <forge/surface_shader.hpp>
#include <set>
namespace forge {
struct GraphNodeIdTag;
struct GraphEdgeIdTag;
struct GraphFunctionIdTag;
using GraphNodeId = PersistentId<GraphNodeIdTag>;
using GraphEdgeId = PersistentId<GraphEdgeIdTag>;
using GraphFunctionId = PersistentId<GraphFunctionIdTag>;
struct MaterialGraphPort {
    std::string key, label;
    MaterialParameterType type{};
};
struct MaterialGraphNodeSchema {
    std::string key, label, category, help;
    unsigned version = 1;
};
// First-party schema discovery is UI independent; unsupported schemas remain
// authored data. No universal graph execution or native editor-plugin ABI.
const std::vector<MaterialGraphNodeSchema>& material_graph_node_schemas();
nlohmann::json create_material_graph_node(std::string_view schema, std::array<double, 2> position);
nlohmann::json create_material_graph();
void validate_material_graph_document(const nlohmann::json&);
std::vector<MaterialGraphPort> material_graph_inputs(const nlohmann::json& node);
std::vector<MaterialGraphPort> material_graph_outputs(const nlohmann::json& node);
struct MaterialGraphDiagnostic {
    std::string code, message, port;
    GraphNodeId node;
};
class MaterialGraphError : public std::runtime_error {
  public:
    explicit MaterialGraphError(MaterialGraphDiagnostic);
    const MaterialGraphDiagnostic& diagnostic() const { return diagnostic_; }

  private:
    MaterialGraphDiagnostic diagnostic_;
};
struct CompiledMaterialGraph {
    SurfaceShaderDefinition surface;
    std::string source;
};
// Functions are pure typed subgraphs owned by the same Shader document.
// Expansion is deterministic and bounded; no runtime VM or asset owner.
nlohmann::json expand_material_graph_functions(const nlohmann::json&);
GraphFunctionId extract_material_graph_function(nlohmann::json&, const std::set<GraphNodeId>&,
                                                std::string label);
nlohmann::json material_graph_function_call(const nlohmann::json& definition,
                                            std::array<double, 2> position);
CompiledMaterialGraph compile_material_graph(const nlohmann::json&);
struct MaterialGraphSource {
    nlohmann::json document;
    AssetId asset() const;
    void validate() const;
    // A new logical Shader owns new graph identities; its binding contract stays compatible.
    MaterialGraphSource duplicate(AssetId) const;
    static MaterialGraphSource parse(std::span<const std::byte>);
    static MaterialGraphSource create(AssetId);
};
// Remap declared node/edge identities only. Preserve opaque extension payloads.
nlohmann::json copy_material_graph_selection(const nlohmann::json&, const std::set<GraphNodeId>&);
void paste_material_graph_selection(nlohmann::json&, const nlohmann::json&,
                                    std::array<double, 2> offset);
} // namespace forge
