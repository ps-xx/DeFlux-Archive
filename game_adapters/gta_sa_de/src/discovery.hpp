#pragma once

#include <string>
#include <vector>

namespace deflux::game::gta_sa_de::detail {

enum class PathKind {
    Missing,
    NotAFile,
    File,
};

PathKind classify_path(const std::wstring& path);

// GetFullPathName + slash unify + GetLongPathName. Empty if not a regular file.
std::wstring canonicalize_file_path(const std::wstring& path);

// Same normalization for comparison; does not require the file to exist.
std::wstring normalize_path_for_compare(const std::wstring& path);

bool executable_paths_equal(const std::wstring& left, const std::wstring& right);

// Steam library + appmanifest discovery. Never writes. No hardcoded user path.
std::wstring discover_gta_sa_de_executable();

bool is_gta_sa_de_executable_name(std::wstring_view filename) noexcept;
bool looks_like_gta_sa_de_layout(const std::wstring& path) noexcept;

} // namespace deflux::game::gta_sa_de::detail
