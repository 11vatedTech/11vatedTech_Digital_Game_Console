#include "dc/title_supervisor.hpp"
#include <cstdio>
using namespace dc;
int main() {
    ITitleSupervisor* s = CreateTitleSupervisor();
    auto r = s->Launch("C:\\Windows\\System32\\cmd.exe", "", "/c ping -n 30 127.0.0.1 >nul");
    printf("launch ok=%d pid=%u err=%s\n", r.ok, r.process_id, r.error.c_str());
    if (!r.ok) return 1;
    printf("running=%d\n", s->IsRunning());
    s->TerminateTree();
    auto rep = s->WaitExit(3000);
    printf("exit kind=%s code=%u cleanup=%d\n", ITitleSupervisor::ExitKindName(rep.kind), rep.exit_code, rep.job_cleanup_complete);
    printf("running after terminate=%d\n", s->IsRunning());
    DestroyTitleSupervisor(s);
    return 0;
}
