// dc/focus_graph.hpp — deterministic controller focus topology (DK0-M2 §33).
// No implicit tab order: every screen declares its focusable nodes and the
// four-directional edges between them, plus confirm/back behavior. The graph
// is pure data so the shell renders it and the tests walk it identically.
#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace dc {

enum class FocusDirection : uint32_t { Up, Down, Left, Right };

enum class FocusAction : uint32_t { Confirm, Back };

struct FocusNode {
    std::string id;   // stable screen-unique id, e.g. "home.play_recent"
    // Direct neighbor ids per direction ("" = edge not connected).
    std::string up;
    std::string down;
    std::string left;
    std::string right;
    // What Confirm does is screen-defined; the graph only routes navigation.
};

class FocusGraph {
public:
    bool AddNode(const FocusNode& node);
    bool HasNode(const std::string& id) const;
    const FocusNode* Node(const std::string& id) const;

    // Move from the current node in a direction. Returns the new node id and
    // sets current(); returns false (and leaves current unchanged) when the
    // edge is not connected — the shell plays a "dead end" cue instead.
    bool Move(FocusDirection dir);

    // Direct neighbor query (testable without mutating state).
    std::string Neighbor(const std::string& from, FocusDirection dir) const;

    const std::string& Current() const { return current_; }
    void SetCurrent(const std::string& id);

    size_t NodeCount() const { return nodes_.size(); }

private:
    std::unordered_map<std::string, FocusNode> nodes_;
    std::string current_;
};

} // namespace dc
