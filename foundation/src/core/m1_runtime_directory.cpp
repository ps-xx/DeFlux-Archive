#include "deflux/core/m1_runtime_directory.hpp"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>

#include <algorithm>
#include <filesystem>
#include <string>

namespace deflux::core {
namespace {

namespace fs = std::filesystem;

struct ContaminantRule {
    const wchar_t* filename;
    const char* label;
};

constexpr ContaminantRule kLegacyRuntimeArtifacts[] = {
    {L"version.dll", "legacy version.dll proxy (VERSION.dll import hijack)"},
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

std::string wide_to_utf8(const std::wstring& wide) {
    if (wide.empty()) {
        return {};
    }
    const int needed = WideCharToMultiByte(
        CP_UTF8, 0, wide.c_str(), static_cast<int>(wide.size()), nullptr, 0, nullptr, nullptr);
    if (needed <= 0) {
        return {};
    }
    std::string utf8(static_cast<std::size_t>(needed), '\0');
    WideCharToMultiByte(
        CP_UTF8, 0, wide.c_str(), static_cast<int>(wide.size()), utf8.data(), needed, nullptr, nullptr);
    return utf8;
}

std::wstring utf8_to_wide(std::string_view utf8) {
    if (utf8.empty()) {
        return {};
    }
    const int needed = MultiByteToWideChar(
        CP_UTF8, MB_ERR_INVALID_CHARS, utf8.data(), static_cast<int>(utf8.size()), nullptr, 0);
    if (needed <= 0) {
        return {};
    }
    std::wstring wide(static_cast<std::size_t>(needed), L'\0');
    MultiByteToWideChar(
        CP_UTF8, MB_ERR_INVALID_CHARS, utf8.data(), static_cast<int>(utf8.size()), wide.data(), needed);
    return wide;
}

void append_contaminant(M1RuntimeDirectoryScan& result,
                        const ContaminantRule& rule,
                        const fs::path& directory) {
    M1RuntimeContaminant entry{};
    entry.filename = wide_to_utf8(rule.filename);
    entry.label = rule.label;
    entry.path = wide_to_utf8((directory / rule.filename).wstring());
    result.contaminants.push_back(std::move(entry));
}

std::string build_diagnostic(const M1RuntimeDirectoryScan& result) {
    if (result.contaminants.empty()) {
        return "M1 runtime directory is free of known legacy loader/runtime artifacts.";
    }

    std::string message =
        "Legacy runtime artifacts detected in the M1 runtime directory. "
        "M1 executables must not run beside legacy version.dll or DeFluxCore modules. "
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

M1RuntimeDirectoryScan scan_m1_runtime_directory(std::string_view directory_utf8) {
    M1RuntimeDirectoryScan result{};
    result.scanned_directory = std::string(directory_utf8);

    if (directory_utf8.empty()) {
        result.message = "Cannot scan an empty M1 runtime directory path.";
        return result;
    }

    const fs::path directory = fs::path(utf8_to_wide(directory_utf8));
    std::error_code ec;
    if (!fs::exists(directory, ec) || !fs::is_directory(directory, ec)) {
        result.message = "M1 runtime directory does not exist or is not a directory.";
        return result;
    }

    result.scanned_directory = wide_to_utf8(directory.wstring());

    for (const ContaminantRule& rule : kLegacyRuntimeArtifacts) {
        if (file_exists_in_directory(directory, rule.filename)) {
            append_contaminant(result, rule, directory);
        }
    }

    std::sort(result.contaminants.begin(),
              result.contaminants.end(),
              [](const M1RuntimeContaminant& left, const M1RuntimeContaminant& right) {
                  return left.filename < right.filename;
              });

    result.clean = result.contaminants.empty();
    result.message = build_diagnostic(result);
    return result;
}

} // namespace deflux::core
