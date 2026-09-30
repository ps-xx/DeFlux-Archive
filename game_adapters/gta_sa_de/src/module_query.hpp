#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace deflux::game::gta_sa_de::detail {

struct MainModule {
    bool found = false;
    std::uint64_t base = 0;
    std::uint64_t size = 0;
    std::wstring path;
    std::string message;
};

struct ModuleRange {
    std::uint64_t base = 0;
    std::uint64_t size = 0;
};

// ASLR-safe main-module discovery via Toolhelp. No hardcoded base.
MainModule query_main_module(std::uint32_t process_id, std::wstring_view expected_exe);

bool query_loaded_module_ranges(std::uint32_t process_id, std::vector<ModuleRange>& out);

} // namespace deflux::game::gta_sa_de::detail
