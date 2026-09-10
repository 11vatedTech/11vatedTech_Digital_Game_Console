// focus_graph.cpp — focus navigation implementation (DK0-M2 §33).
#include "dc/focus_graph.hpp"

namespace dc {

bool FocusGraph::AddNode(const FocusNode& node) {
    if (node.id.empty()) return false;
    auto [it, inserted] = nodes_.emplace(node.id, node);
    if (nodes_.size() == 1) current_ = node.id; // first node becomes default focus
    return inserted;
}

bool FocusGraph::HasNode(const std::string& id) const {
    return nodes_.count(id) != 0;
}

const FocusNode* FocusGraph::Node(const std::string& id) const {
    auto it = nodes_.find(id);
    return it == nodes_.end() ? nullptr : &it->second;
}

std::string FocusGraph::Neighbor(const std::string& from, FocusDirection dir) const {
    const FocusNode* n = Node(from);
    if (!n) return "";
    switch (dir) {
        case FocusDirection::Up: return n->up;
        case FocusDirection::Down: return n->down;
        case FocusDirection::Left: return n->left;
        case FocusDirection::Right: return n->right;
    }
    return "";
}

bool FocusGraph::Move(FocusDirection dir) {
    std::string next = Neighbor(current_, dir);
    if (next.empty() || !HasNode(next)) return false;
    current_ = next;
    return true;
}

void FocusGraph::SetCurrent(const std::string& id) {
    if (HasNode(id)) current_ = id;
}

} // namespace dc
