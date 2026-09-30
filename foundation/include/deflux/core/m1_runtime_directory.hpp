#pragma once

#include <string>
#include <string_view>
#include <vector>

namespace deflux::core {

struct M1RuntimeContaminant {
    std::string filename;
    std::string label;
    std::string path;
};

struct M1RuntimeDirectoryScan {
    bool clean = false;
    std::string scanned_directory;
    std::string message;
    std::vector<M1RuntimeContaminant> contaminants;
};

// Filesystem-only scan of an M1 runtime output directory. Does not load DLLs,
// inject, or inspect GTA. Independent of GTA Win64 environment preflight.
M1RuntimeDirectoryScan scan_m1_runtime_directory(std::string_view directory_utf8);

} // namespace deflux::core
