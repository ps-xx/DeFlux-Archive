#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>

#include <string>

namespace {

HMODULE g_real_version = nullptr;
HMODULE g_self = nullptr;

FARPROC resolve(const char* name) {
    return g_real_version != nullptr ? GetProcAddress(g_real_version, name) : nullptr;
}

bool load_real_version() {
    if (g_real_version != nullptr) {
        return true;
    }

    wchar_t sys[MAX_PATH]{};
    const UINT len = GetSystemDirectoryW(sys, MAX_PATH);
    if (len == 0 || len >= MAX_PATH) {
        return false;
    }

    std::wstring path(sys);
    if (path.back() != L'\\' && path.back() != L'/') {
        path.push_back(L'\\');
    }
    path += L"version.dll";

    g_real_version = LoadLibraryW(path.c_str());
    return g_real_version != nullptr;
}

std::wstring module_dir() {
    wchar_t path[MAX_PATH]{};
    if (g_self == nullptr || GetModuleFileNameW(g_self, path, MAX_PATH) == 0) {
        return {};
    }
    std::wstring full(path);
    const auto slash = full.find_last_of(L"\\/");
    if (slash == std::wstring::npos) {
        return {};
    }
    return full.substr(0, slash + 1);
}

// Load FusionFix / other Win64\scripts\*.asi early so SkipMenu can catch the SA title logo.
void load_scripts_folder(const std::wstring& dir) {
    const std::wstring scripts = dir + L"scripts\\";
    const std::wstring pattern = scripts + L"*.asi";

    WIN32_FIND_DATAW fd{};
    const HANDLE find = FindFirstFileW(pattern.c_str(), &fd);
    if (find == INVALID_HANDLE_VALUE) {
        return;
    }

    do {
        if ((fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0) {
            continue;
        }
        const std::wstring path = scripts + fd.cFileName;
        LoadLibraryW(path.c_str());
    } while (FindNextFileW(find, &fd));

    FindClose(find);
}

DWORD WINAPI load_plugins(LPVOID) {
    // FusionFix must hook early (SkipMenu watches menu_mainpresents / ResumeGameFromMenu).
    // Tiny quiet period only — do not wait for the title screen.
    Sleep(1500);

    const std::wstring dir = module_dir();
    if (dir.empty()) {
        return 1;
    }

    // FusionFix SkipMenu / SkipIntro (and any other scripts\*.asi) — code path, no keys.
    load_scripts_folder(dir);

    // DeFlux core soon after — needs to confirm New Game on the main menu while
    // story saves are still quarantined (long delays left the user stuck on START).
    Sleep(4000);

    if (GetModuleHandleW(L"DeFluxCore.asi") != nullptr
        || GetModuleHandleW(L"DeFluxCore.dll") != nullptr) {
        return 0;
    }

    HMODULE mod = LoadLibraryW((dir + L"DeFluxCore.asi").c_str());
    if (mod == nullptr) {
        mod = LoadLibraryW((dir + L"DeFluxCore.dll").c_str());
    }
    return mod != nullptr ? 0 : 2;
}

template <typename Fn>
Fn get_fn(const char* name) {
    auto* fn = reinterpret_cast<Fn>(resolve(name));
    if (fn == nullptr && load_real_version()) {
        fn = reinterpret_cast<Fn>(resolve(name));
    }
    return fn;
}

} // namespace

extern "C" {

BOOL WINAPI DeFlux_GetFileVersionInfoA(LPCSTR a, DWORD b, DWORD c, LPVOID d) {
    using Fn = BOOL(WINAPI*)(LPCSTR, DWORD, DWORD, LPVOID);
    const Fn fn = get_fn<Fn>("GetFileVersionInfoA");
    return fn ? fn(a, b, c, d) : FALSE;
}

BOOL WINAPI DeFlux_GetFileVersionInfoW(LPCWSTR a, DWORD b, DWORD c, LPVOID d) {
    using Fn = BOOL(WINAPI*)(LPCWSTR, DWORD, DWORD, LPVOID);
    const Fn fn = get_fn<Fn>("GetFileVersionInfoW");
    return fn ? fn(a, b, c, d) : FALSE;
}

DWORD WINAPI DeFlux_GetFileVersionInfoSizeA(LPCSTR a, LPDWORD b) {
    using Fn = DWORD(WINAPI*)(LPCSTR, LPDWORD);
    const Fn fn = get_fn<Fn>("GetFileVersionInfoSizeA");
    return fn ? fn(a, b) : 0;
}

DWORD WINAPI DeFlux_GetFileVersionInfoSizeW(LPCWSTR a, LPDWORD b) {
    using Fn = DWORD(WINAPI*)(LPCWSTR, LPDWORD);
    const Fn fn = get_fn<Fn>("GetFileVersionInfoSizeW");
    return fn ? fn(a, b) : 0;
}

BOOL WINAPI DeFlux_VerQueryValueA(LPCVOID a, LPCSTR b, LPVOID* c, PUINT d) {
    using Fn = BOOL(WINAPI*)(LPCVOID, LPCSTR, LPVOID*, PUINT);
    const Fn fn = get_fn<Fn>("VerQueryValueA");
    return fn ? fn(a, b, c, d) : FALSE;
}

BOOL WINAPI DeFlux_VerQueryValueW(LPCVOID a, LPCWSTR b, LPVOID* c, PUINT d) {
    using Fn = BOOL(WINAPI*)(LPCVOID, LPCWSTR, LPVOID*, PUINT);
    const Fn fn = get_fn<Fn>("VerQueryValueW");
    return fn ? fn(a, b, c, d) : FALSE;
}

BOOL WINAPI DeFlux_GetFileVersionInfoExA(DWORD a, LPCSTR b, DWORD c, DWORD d, LPVOID e) {
    using Fn = BOOL(WINAPI*)(DWORD, LPCSTR, DWORD, DWORD, LPVOID);
    const Fn fn = get_fn<Fn>("GetFileVersionInfoExA");
    return fn ? fn(a, b, c, d, e) : FALSE;
}

BOOL WINAPI DeFlux_GetFileVersionInfoExW(DWORD a, LPCWSTR b, DWORD c, DWORD d, LPVOID e) {
    using Fn = BOOL(WINAPI*)(DWORD, LPCWSTR, DWORD, DWORD, LPVOID);
    const Fn fn = get_fn<Fn>("GetFileVersionInfoExW");
    return fn ? fn(a, b, c, d, e) : FALSE;
}

DWORD WINAPI DeFlux_GetFileVersionInfoSizeExA(DWORD a, LPCSTR b, LPDWORD c) {
    using Fn = DWORD(WINAPI*)(DWORD, LPCSTR, LPDWORD);
    const Fn fn = get_fn<Fn>("GetFileVersionInfoSizeExA");
    return fn ? fn(a, b, c) : 0;
}

DWORD WINAPI DeFlux_GetFileVersionInfoSizeExW(DWORD a, LPCWSTR b, LPDWORD c) {
    using Fn = DWORD(WINAPI*)(DWORD, LPCWSTR, LPDWORD);
    const Fn fn = get_fn<Fn>("GetFileVersionInfoSizeExW");
    return fn ? fn(a, b, c) : 0;
}

} // extern "C"

BOOL APIENTRY DllMain(HMODULE hModule, DWORD reason, LPVOID) {
    switch (reason) {
    case DLL_PROCESS_ATTACH:
        DisableThreadLibraryCalls(hModule);
        g_self = hModule;
        if (!load_real_version()) {
            return FALSE;
        }
        CreateThread(nullptr, 0, load_plugins, nullptr, 0, nullptr);
        break;
    case DLL_PROCESS_DETACH:
        g_real_version = nullptr;
        break;
    default:
        break;
    }
    return TRUE;
}
