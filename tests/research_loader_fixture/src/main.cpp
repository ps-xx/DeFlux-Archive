#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>

#include <cstdlib>
#include <cstring>

int main(int argc, char** argv) {
    DWORD wait_ms = 60000;
    for (int i = 1; i < argc; ++i) {
        if (argv[i] != nullptr && std::strcmp(argv[i], "--wait-ms") == 0 && i + 1 < argc) {
            wait_ms = static_cast<DWORD>(std::strtoul(argv[i + 1], nullptr, 10));
            break;
        }
    }

    DWORD elapsed = 0;
    while (elapsed < wait_ms) {
        Sleep(100);
        elapsed += 100;
    }
    return 0;
}
