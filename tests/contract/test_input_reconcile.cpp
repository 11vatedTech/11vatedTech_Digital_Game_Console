// test_input_reconcile.cpp — controller dedup policy tests (§K scenarios):
// single controller, reconnect, backend duplicate, controller handoff, two
// controllers, Bluetooth reconnect. The policy is pure — no hardware needed;
// live single-source behavior is separately covered by input_router_live.
#include "dc/input_reconcile.hpp"

#include <cstdio>
#include <string>
#include <vector>

using dc::BackendPriority;
using dc::ReconcileDevice;
using dc::ReconciledDevice;

namespace {

int g_failures = 0;

void Check(bool ok, const char* what) {
    std::printf("  %s %s\n", ok ? "PASS" : "FAIL", what);
    if (!ok) ++g_failures;
}

ReconcileDevice Gi(const std::string& id, const std::string& hw = "") {
    return {"gameinput", id, hw};
}
ReconcileDevice Sdl(const std::string& id, const std::string& hw = "") {
    return {"sdl3", id, hw};
}
ReconcileDevice Xi(const std::string& id, const std::string& hw = "") {
    return {"xinput", id, hw};
}

size_t CountPlayer(const std::vector<dc::ReconciledDevice>& devs,
                   uint32_t player) {
    size_t n = 0;
    for (const auto& d : devs)
        if (d.player == player) ++n;
    return n;
}

} // namespace

int main() {
    std::printf("[input reconcile]\n");

    // 1. Single controller through the primary backend.
    {
        auto r = dc::ReconcileControllers({Gi("pad-a")}, {}, {});
        Check(r.size() == 1 && r[0].player == 1 && r[0].backend == "gameinput",
              "single controller → one player via primary");
    }

    // 2. Backend duplicate: the same physical pad reported by GameInput and
    //    SDL3 (matched by hardware id) must surface exactly once.
    {
        auto r = dc::ReconcileControllers({Gi("pad-a", "BT:AA:BB")},
                                          {Sdl("sdl-0", "BT:AA:BB")}, {});
        Check(r.size() == 1, "backend duplicate collapsed");
        Check(!r.empty() && r[0].backend == "gameinput",
              "primary backend wins the duplicate");
    }

    // 3. Backend duplicate without hardware id: conservative suppression.
    //    A console must never emit a ghost second player; the lower backend
    //    occurrence is presumed to be the same physical pad (§K invariant).
    {
        auto r = dc::ReconcileControllers({Gi("pad-a")}, {Sdl("sdl-0")}, {});
        Check(r.size() == 1 && r[0].backend == "gameinput",
              "unknown identity under claimed primary → suppressed");
    }

    // 3b. Distinct known hardware identity under a claimed higher backend
    //     is a genuinely different pad and must be accepted.
    {
        auto r = dc::ReconcileControllers({Gi("pad-a", "HW:1")},
                                          {Sdl("sdl-0", "HW:2")}, {});
        Check(r.size() == 2 && r[1].backend == "sdl3" && r[1].player == 2,
              "distinct known identity → second pad accepted");
    }

    // 4. Two controllers stay two players.
    {
        auto r = dc::ReconcileControllers({Gi("pad-a", "HW:1"), Gi("pad-b", "HW:2")},
                                          {}, {});
        Check(r.size() == 2 && r[0].player == 1 && r[1].player == 2,
              "two controllers → two players");
    }

    // 5. Controller handoff: pad disconnects from primary and reappears on
    //    the secondary backend — it must be adopted exactly once.
    {
        auto r = dc::ReconcileControllers({}, {Sdl("sdl-0", "HW:1")}, {});
        Check(r.size() == 1 && r[0].player == 1 && r[0].backend == "sdl3",
              "handoff to secondary adopted once");
    }

    // 6. Reconnect: same pad returning restores the same slot when it is the
    //    only device (deterministic ordering → same assignment).
    {
        auto before = dc::ReconcileControllers({Gi("pad-a")}, {}, {});
        auto after = dc::ReconcileControllers({Gi("pad-a")}, {}, {});
        Check(before.size() == 1 && after.size() == 1 &&
                  before[0].player == after[0].player,
              "reconnect restores player slot");
    }

    // 7. Bluetooth reconnect identity: the same hardware id must reconcile
    //    to one device even when the backend-local id changes.
    {
        auto r = dc::ReconcileControllers({Gi("pad#a1b2", "BT:AA:BB")},
                                          {Sdl("sdl-9", "BT:AA:BB")}, {});
        Check(r.size() == 1 && r[0].backend == "gameinput",
              "Bluetooth reconnect identity resolves to primary");
    }

    // 8. Legacy floor: XInput pads accepted when nothing else claims them,
    //    and never doubled when SDL3 already reported the same hardware.
    {
        auto r = dc::ReconcileControllers({}, {}, {Xi("xinput-0", "HW:1"),
                                                   Xi("xinput-1", "HW:2")});
        Check(r.size() == 2 && CountPlayer(r, 1) == 1 && CountPlayer(r, 2) == 1,
              "legacy floor assigns distinct players");

        auto r2 = dc::ReconcileControllers({}, {Sdl("sdl-0", "HW:1")},
                                           {Xi("xinput-0", "HW:1")});
        Check(r2.size() == 1 && r2[0].backend == "sdl3",
              "duplicate across secondary/legacy resolves to secondary");
    }

    // 9. Same-backend hiccup: identical id reported twice collapses.
    {
        auto r = dc::ReconcileControllers({Gi("pad-a"), Gi("pad-a")}, {}, {});
        Check(r.size() == 1, "same-backend duplicate collapsed");
    }

    // 10. Empty inputs → empty output; no invented devices.
    {
        auto r = dc::ReconcileControllers({}, {}, {});
        Check(r.empty(), "no devices → no players");
    }

    // 11. Determinism: identical input produces byte-identical output twice.
    {
        auto a = dc::ReconcileControllers({Gi("pad-b", "HW:2"), Gi("pad-a", "HW:1")},
                                          {Sdl("sdl-5")}, {Xi("xinput-0")});
        auto b = dc::ReconcileControllers({Gi("pad-b", "HW:2"), Gi("pad-a", "HW:1")},
                                          {Sdl("sdl-5")}, {Xi("xinput-0")});
        bool same = a.size() == b.size();
        for (size_t i = 0; same && i < a.size(); ++i) {
            same = a[i].backend == b[i].backend && a[i].id == b[i].id &&
                   a[i].player == b[i].player;
        }
        Check(same, "deterministic assignment across runs");
    }

    std::printf("%s\n", g_failures ? "RESULT: FAIL" : "RESULT: all passed");
    return g_failures ? 1 : 0;
}
