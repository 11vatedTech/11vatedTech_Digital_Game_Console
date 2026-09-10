#include "dc/focus_graph.hpp"
#include <cassert>
#include <cstdio>
using namespace dc;
int main() {
    FocusGraph g;
    assert(g.AddNode({"home_play", "home_library", "", "home_system", "home_play"}));
    assert(!g.AddNode({"home_play", "", "", "", ""})); // duplicate rejected
    assert(g.AddNode({"home_library", "home_play", "home_system", "home_play", ""}));
    assert(g.AddNode({"home_system", "", "", "home_play", "home_library"}));
    assert(g.NodeCount() == 3);
    assert(g.Current() == "home_play");
    // Right from play -> library
    assert(g.Move(FocusDirection::Right));
    assert(g.Current() == "home_library");
    // Left -> play
    assert(g.Move(FocusDirection::Left));
    assert(g.Current() == "home_play");
    // Left edge (none) fails, current unchanged
    assert(!g.Move(FocusDirection::Left));
    assert(g.Current() == "home_play");
    // Disconnected direction fails
    assert(!g.Move(FocusDirection::Down));
    assert(g.Neighbor("home_play", FocusDirection::Up) == "home_library");
    assert(g.Neighbor("nope", FocusDirection::Up).empty());
    g.SetCurrent("home_system");
    assert(g.Current() == "home_system");
    g.SetCurrent("nonexistent");
    assert(g.Current() == "home_system"); // invalid set ignored
    std::printf("focus graph tests passed\n");
    return 0;
}
