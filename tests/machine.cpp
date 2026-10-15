#include "core/machine.h"
#include <iostream>
#include <type_traits>
using namespace oceanblast;
static int failures=0;
static void check(const char* name,bool ok) {
    std::cout << (ok?"PASS ":"FAIL ") << name << '\n';
    failures+=!ok;
}
int main() {
    static_assert(!std::is_move_constructible<Machine>::value,"Machine references must remain stable");
    Machine machine;
    machine.bus().write32(0,0xe2800001); machine.bus().write32(4,0xeafffffd);
    machine.configureExecution(false,3);
    const size_t retired=machine.execution().run(101);
    check("Machine executes a bounded program without frontend services",
        retired==101 && machine.cpu().getReg(0)==51 && machine.execution().time().ticks==34);
    Machine independent;
    check("Separate machines do not share registers memory or execution time",
        independent.cpu().getReg(0)==0 && independent.bus().read32(0)==0 && independent.execution().time().ticks==0);
    machine.configureExecution(true,1);
    check("Timing configuration preserves guest state and starts a fresh timeline",
        machine.cpu().getReg(0)==51 && machine.bus().read32(0)==0xe2800001 && machine.execution().time().ticks==0);
    bool rejected=false;
    try { machine.configureExecution(false,0); } catch (const std::invalid_argument&) { rejected=true; }
    check("Invalid timing configuration leaves the machine usable",
        rejected && machine.execution().run(1)==1 && machine.cpu().getPC()==0);
    std::cout << failures << " failure(s)\n";
    return failures?1:0;
}
