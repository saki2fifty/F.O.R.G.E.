#pragma once
#include <map>
#include <string>
#include <vector>
namespace forge::detail {
struct RelationshipEdge {
    std::string target;
    // An IsA edge in the mixed expansion graph replaces a source node; it does
    // not add a level to the instantiated structural tree. IsA depth itself is
    // checked separately with ordinary advancing edges.
    bool advances_depth = true;
};
using RelationshipGraph = std::map<std::string, std::vector<RelationshipEdge>>;
// Validate before any native relationship realization. Iterative topology/depth
// checks avoid recursion on untrusted documents and respect the pinned Flecs DAG
// profile even when upstream assertions are compiled out in Release builds.
void validate_relationship_graph(const RelationshipGraph&, const char* relationship,
                                 unsigned reserved_depth = 0);
} // namespace forge::detail
