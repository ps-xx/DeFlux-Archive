#include "environment_preflight.hpp"

#include "deflux/game/gta_sa_de/grounded_live_research.hpp"
#include "sha256.hpp"
#include "win_utf8.hpp"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>

#include <algorithm>
#include <filesystem>

namespace deflux::game::gta_sa_de::detail {
namespace {

namespace fs = std::filesystem;

struct ContaminantRule {
    const wchar_t* filename;
    const char* label;
};

// DeFlux legacy runtime + common ASI/proxy loaders. Do not add Rockstar/Steam
// vanilla DLLs that legitimately ship beside SanAndreas.exe.
constexpr ContaminantRule kContaminants[] = {
    {L"version.dll", "legacy version.dll proxy"},
    {L"DeFluxCore.asi", "legacy DeFluxCore ASI module"},
    {L"DeFluxCore.dll", "legacy DeFluxCore DLL module"},
    {L"DeFluxCore.log", "legacy DeFluxCore session log"},
    {L"deflux_entry_mode.txt", "legacy DeFlux entry-mode marker"},
    {L"dinput8.dll", "ASI loader (dinput8 proxy)"},
    {L"winmm.dll", "ASI/proxy loader (winmm hijack)"},
    {L"dxgi.dll", "ASI/proxy loader (dxgi hijack)"},
};

bool file_exists_in_directory(const fs::path& directory, const wchar_t* filename) {
    if (directory.empty() || filename == nullptr || filename[0] == L'\0') {
        return false;
    }
    std::error_code ec;
    return fs::is_regular_file(directory / filename, ec);
}

bool official_cleo_loader(const fs::path& file) {
    std::string sha;
    std::uint64_t size = 0;
    if (!sha256_file(file.wstring(), sha, size) || sha.empty()) {
        return false;
    }
    return grounded_asi_loader_sha_is_official(sha);
}

void append_contaminant(EnvironmentPreflightResult& result,
                        const ContaminantRule& rule,
                        const fs::path& directory) {
    EnvironmentContaminant entry{};
    entry.filename = wide_to_utf8(rule.filename);
    entry.label = rule.label;
    entry.path = wide_to_utf8((directory / rule.filename).wstring());
    result.contaminants.push_back(std::move(entry));
}

std::string build_diagnostic(const EnvironmentPreflightResult& result) {
    if (result.contaminants.empty()) {
        return "Game executable directory is free of known legacy DeFlux contamination.";
    }

    std::string message =
        "Legacy runtime contamination detected in the GTA executable directory. "
        "Remove the listed artifacts before live runtime qualification. "
        "Detected: ";
    for (std::size_t i = 0; i < result.contaminants.size(); ++i) {
        if (i != 0) {
            message += ", ";
        }
        message += result.contaminants[i].filename;
        message += " (";
        message += result.contaminants[i].label;
        message += ')';
    }
    return message;
}

} // namespace

EnvironmentPreflightResult scan_game_executable_directory(std::string_view executable_utf8_path) {
    EnvironmentPreflightResult result{};
    result.scanned_directory = std::string(executable_utf8_path);

    if (executable_utf8_path.empty()) {
        result.message = "Cannot scan an empty executable path.";
        return result;
    }

    const fs::path executable = fs::path(utf8_to_wide(executable_utf8_path));
    const fs::path directory = executable.parent_path();
    if (directory.empty()) {
        result.message = "Cannot determine the GTA executable directory.";
        return result;
    }

    result.scanned_directory = wide_to_utf8(directory.wstring());

    for (const ContaminantRule& rule : kContaminants) {
        if (!file_exists_in_directory(directory, rule.filename)) {
            continue;
        }
        // The recorded CLEO 64-bit loader is Ultimate ASI Loader, installed as
        // version.dll. Any other version.dll, including a legacy DeFlux proxy,
        // remains contamination. dinput8/winmm/dxgi are never exempted.
        if (std::wcscmp(rule.filename, L"version.dll") == 0
            && official_cleo_loader(directory / rule.filename)) {
            continue;
        }
        append_contaminant(result, rule, directory);
    }

    std::sort(result.contaminants.begin(),
              result.contaminants.end(),
              [](const EnvironmentContaminant& left, const EnvironmentContaminant& right) {
                  return left.filename < right.filename;
              });

    result.clean = result.contaminants.empty();
    result.message = build_diagnostic(result);
    return result;
}

} // namespace deflux::game::gta_sa_de::detail
