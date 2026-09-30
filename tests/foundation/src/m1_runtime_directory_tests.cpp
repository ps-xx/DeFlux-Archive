#include "deflux/core/m1_runtime_directory.hpp"
#include "deflux/game/gta_sa_de/qualification.hpp"

#include <cstdio>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <string>

namespace deflux_tests {
namespace {

namespace fs = std::filesystem;

int g_failed = 0;
int g_passed = 0;

void check(bool condition, const char* expression, const char* file, int line) {
    if (condition) {
        ++g_passed;
        return;
    }
    ++g_failed;
    std::printf("FAIL %s:%d: %s\n", file, line, expression);
}

#define CHECK_M1(expr) check(static_cast<bool>(expr), #expr, __FILE__, __LINE__)

class TempSandbox {
public:
    TempSandbox() {
        root_ = fs::temp_directory_path()
            / ("deflux_m1_runtime_" + std::to_string(static_cast<unsigned long long>(std::time(nullptr)))
               + "_" + std::to_string(counter_++));
        fs::create_directories(root_);
    }

    ~TempSandbox() {
        std::error_code ec;
        fs::remove_all(root_, ec);
    }

    fs::path dir() const { return root_; }

    void touch(const wchar_t* name) {
        std::ofstream out(root_ / name, std::ios::binary);
        out.put('x');
    }

private:
    static inline int counter_ = 0;
    fs::path root_;
};

void test_clean_m1_runtime_directory_accepted() {
    TempSandbox sandbox;
    const auto scan = deflux::core::scan_m1_runtime_directory(sandbox.dir().string());
    CHECK_M1(scan.clean);
    CHECK_M1(scan.contaminants.empty());
}

void test_version_dll_rejected_in_m1_runtime_directory() {
    TempSandbox sandbox;
    sandbox.touch(L"version.dll");
    const auto scan = deflux::core::scan_m1_runtime_directory(sandbox.dir().string());
    CHECK_M1(!scan.clean);
    CHECK_M1(scan.contaminants.size() == 1);
    CHECK_M1(scan.contaminants[0].filename == "version.dll");
}

void test_defluxcore_asi_rejected_in_m1_runtime_directory() {
    TempSandbox sandbox;
    sandbox.touch(L"DeFluxCore.asi");
    const auto scan = deflux::core::scan_m1_runtime_directory(sandbox.dir().string());
    CHECK_M1(!scan.clean);
    CHECK_M1(scan.contaminants[0].filename == "DeFluxCore.asi");
}

void test_defluxcore_dll_rejected_in_m1_runtime_directory() {
    TempSandbox sandbox;
    sandbox.touch(L"DeFluxCore.dll");
    const auto scan = deflux::core::scan_m1_runtime_directory(sandbox.dir().string());
    CHECK_M1(!scan.clean);
    CHECK_M1(scan.contaminants[0].filename == "DeFluxCore.dll");
}

void test_multiple_contaminants_sorted() {
    TempSandbox sandbox;
    sandbox.touch(L"DeFluxCore.dll");
    sandbox.touch(L"version.dll");
    sandbox.touch(L"DeFluxCore.asi");
    const auto scan = deflux::core::scan_m1_runtime_directory(sandbox.dir().string());
    CHECK_M1(!scan.clean);
    CHECK_M1(scan.contaminants.size() == 3);
    CHECK_M1(scan.contaminants[0].filename == "DeFluxCore.asi");
    CHECK_M1(scan.contaminants[1].filename == "DeFluxCore.dll");
    CHECK_M1(scan.contaminants[2].filename == "version.dll");
}

void test_gta_environment_preflight_remains_independent() {
    TempSandbox sandbox;
    const fs::path exe = sandbox.dir() / L"SanAndreas.exe";
    {
        std::ofstream out(exe, std::ios::binary);
        out.put('M');
        out.put('Z');
    }

    const auto m1_scan = deflux::core::scan_m1_runtime_directory(sandbox.dir().string());
    CHECK_M1(m1_scan.clean);

    sandbox.touch(L"version.dll");
    const auto gta_preflight =
        deflux::game::gta_sa_de::scan_game_executable_directory(exe.string());
    CHECK_M1(!gta_preflight.clean);
    CHECK_M1(gta_preflight.contaminants[0].filename == "version.dll");

    const auto m1_after = deflux::core::scan_m1_runtime_directory(sandbox.dir().string());
    CHECK_M1(!m1_after.clean);
}

} // namespace

int run_m1_runtime_directory_tests(int& passed, int& failed) {
    const int before_passed = g_passed;
    const int before_failed = g_failed;

    test_clean_m1_runtime_directory_accepted();
    test_version_dll_rejected_in_m1_runtime_directory();
    test_defluxcore_asi_rejected_in_m1_runtime_directory();
    test_defluxcore_dll_rejected_in_m1_runtime_directory();
    test_multiple_contaminants_sorted();
    test_gta_environment_preflight_remains_independent();

    passed += g_passed - before_passed;
    failed += g_failed - before_failed;
    return failed == before_failed ? 0 : 1;
}

} // namespace deflux_tests
