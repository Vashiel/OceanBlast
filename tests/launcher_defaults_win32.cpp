#include "display/launcher.h"
#include <iostream>
static int failures = 0;
static void CALLBACK inspect(HWND, UINT, UINT_PTR timer, DWORD) {
    using namespace oceanblast::launcher;
    auto check = [](const char* name, bool ok) { std::cout << (ok ? "PASS " : "FAIL ") << name << '\n'; failures += !ok; };
    check("Launcher audio enabled", SendMessageW(soundBox, BM_GETCHECK,0,0) == BST_CHECKED);
    check("Launcher automatic ROM settings", SendMessageW(timingBox,CB_GETCURSEL,0,0) == 0);
    check("Launcher plain window", SendMessageW(windowModeBox,CB_GETCURSEL,0,0) == 2);
    KillTimer(nullptr,timer);
    PostMessageW(GetParent(soundBox),WM_CLOSE,0,0);
}
int main() { SetTimer(nullptr,0,200,inspect); const int result=oceanblast::launcher::run(); return result || failures ? 1 : 0; }
