#include "dc/input.hpp"
#include <cstdio>
namespace dc {
IInputRouter* CreateGameInputRouter();
void DestroyGameInputRouter(IInputRouter*);
}
int main() {
    dc::IInputRouter* r = dc::CreateGameInputRouter();
    if (!r) { printf("router create failed\n"); return 1; }
    for (int i = 0; i < 10; ++i) {
        dc::ConsoleAction a = r->Poll(dc::ConsoleAction::None);
        auto st = r->StateFor(0);
        if (i == 0) printf("frame0: actions=%u devices=%zu buttons=%u\n",
                           (unsigned)a, r->DeviceCount(), st.buttons);
    }
    printf("10 frames polled without crash\n");
    dc::DestroyGameInputRouter(r);
    return 0;
}
