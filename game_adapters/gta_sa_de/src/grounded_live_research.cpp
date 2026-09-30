#include "deflux/game/gta_sa_de/grounded_live_research.hpp"

#include "deflux/game/gta_sa_de/adapter.hpp"
#include "deflux/game/gta_sa_de/grounded_transform_research.hpp"
#include "deflux/game/gta_sa_de/qualification.hpp"
#include "deflux/game/gta_sa_de/research_build_allowlist.hpp"
#include "deflux/game/gta_sa_de/research_intent.hpp"
#include "deflux/core/process_lifecycle.hpp"
#include "discovery.hpp"
#include "fingerprint.hpp"
#include "module_query.hpp"
#include "process_query.hpp"
#include "sha256.hpp"
#include "win_utf8.hpp"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include <TlHelp32.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

namespace deflux::game::gta_sa_de {
namespace {

constexpr DWORD kLiveAccess = PROCESS_QUERY_LIMITED_INFORMATION | PROCESS_VM_READ;
static_assert((kLiveAccess & (PROCESS_VM_WRITE | PROCESS_VM_OPERATION | PROCESS_CREATE_THREAD)) == 0,
              "Task #14C process access must stay read-only.");
static_assert(kLiveAccess == (0x1000u | 0x0010u), "Unexpected process-access mask.");

constexpr std::uint64_t kScanWindow = 8ull * 1024ull * 1024ull;
constexpr std::uint32_t kPrivate = 0x20000;

struct NamedModule {
    std::uint64_t base = 0;
    std::uint64_t size = 0;
    GroundedModuleKind kind = GroundedModuleKind::GameImage;
    std::string name;
};

struct LiveHit {
    std::uint64_t address = 0;
    std::uint64_t region_base = 0;
    std::uint32_t region_type = 0;
    double x = 0.0;
    double y = 0.0;
    double z = 0.0;
    double offset_x = 0.0;
    double offset_y = 0.0;
    double offset_z = 0.0;
    bool marker_allocation = false;
    std::uint32_t initial_page_matches = 1;
    std::uint32_t qualified_page_matches = 1;
    bool alive = true;
    GroundedEvidence evidence = GroundedEvidence::Unclassified;
    const char* reason = "";
    std::string module_name;
    SlotRead slots[static_cast<std::size_t>(GroundedSlot::Count)]{};
};

struct ScanOutput {
    bool aborted = false;
    const char* abort_reason = "";
    std::uint64_t bytes_considered = 0;
    std::vector<LiveHit> hits;
    std::vector<Float64Diagnostic> float64_hits;
    GroundedScanTiming timing{};
    std::uint64_t scan_started_ms = 0;
};

bool equals_ignore_case(std::string_view left, std::string_view right) {
    if (left.size() != right.size()) {
        return false;
    }
    for (std::size_t index = 0; index < left.size(); ++index) {
        char a = left[index];
        char b = right[index];
        if (a >= 'A' && a <= 'Z') {
            a = static_cast<char>(a - 'A' + 'a');
        }
        if (b >= 'A' && b <= 'Z') {
            b = static_cast<char>(b - 'A' + 'a');
        }
        if (a != b) {
            return false;
        }
    }
    return true;
}

const char* block_text(GroundedLiveBlock block) {
    switch (block) {
    case GroundedLiveBlock::Ready:
        return "Ready.";
    case GroundedLiveBlock::IsolationEngaged:
        return "Live GTA resolution is isolated from automated tests.";
    case GroundedLiveBlock::IntentMissing:
        return "Task #14C requires the explicit --grounded-live intent.";
    case GroundedLiveBlock::TargetAbsent:
        return "TARGET NOT RUNNING. No matching process is running. No process was opened for memory read.";
    case GroundedLiveBlock::BuildSizeMismatch:
        return "ABORT. Executable file size does not match 1.0.113.21181.";
    case GroundedLiveBlock::BuildShaMismatch:
        return "ABORT. Executable SHA-256 does not match the research allowlist.";
    case GroundedLiveBlock::BuildVersionMismatch:
        return "ABORT. Executable file version does not match 1.0.113.21181.";
    case GroundedLiveBlock::ProcessPathMismatch:
        return "ABORT. Process image path does not match the verified executable.";
    case GroundedLiveBlock::CreationMissing:
        return "ABORT. Process creation identity is missing.";
    case GroundedLiveBlock::ModuleIdentityMismatch:
        return "ABORT. Main module identity does not match the on-disk executable.";
    case GroundedLiveBlock::WriteAccessRejected:
        return "ABORT. Write-capable process access is rejected.";
    }
    return "ABORT. Live gate rejected the session.";
}

bool decode_f32(const std::uint8_t* raw, double& out) {
    float value = 0.0f;
    std::memcpy(&value, raw, sizeof(value));
    out = static_cast<double>(value);
    return std::isfinite(out);
}

bool decode_f64(const std::uint8_t* raw, double& out) {
    double value = 0.0;
    std::memcpy(&value, raw, sizeof(value));
    out = value;
    return std::isfinite(out);
}

GroundedModuleKind classify_module_name(std::string_view name) {
    if (equals_ignore_case(name, "cleo_redux64.asi")) {
        return GroundedModuleKind::CleoAsi;
    }
    if (name.size() >= 5 && equals_ignore_case(name.substr(name.size() - 5), ".cleo")) {
        return GroundedModuleKind::CleoPlugin;
    }
    if (equals_ignore_case(name, "DeFlux_Runtime.dll")
        || equals_ignore_case(name, "DeFlux_ResearchLauncher.exe")) {
        return GroundedModuleKind::ResearchModule;
    }
    return GroundedModuleKind::GameImage;
}

bool query_named_modules(std::uint32_t process_id, std::vector<NamedModule>& out) {
    out.clear();
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32, process_id);
    if (snap == INVALID_HANDLE_VALUE) {
        return false;
    }
    MODULEENTRY32W entry{};
    entry.dwSize = sizeof(entry);
    if (!Module32FirstW(snap, &entry)) {
        CloseHandle(snap);
        return false;
    }
    do {
        NamedModule module{};
        module.base = reinterpret_cast<std::uint64_t>(entry.modBaseAddr);
        module.size = static_cast<std::uint64_t>(entry.modBaseSize);
        const std::wstring wide = entry.szModule;
        module.name = detail::wide_to_utf8(wide);
        module.kind = classify_module_name(module.name);
        if (module.base != 0 && module.size != 0) {
            out.push_back(std::move(module));
        }
    } while (Module32NextW(snap, &entry));
    CloseHandle(snap);
    return true;
}

std::vector<GroundedModule> as_grounded_modules(const std::vector<NamedModule>& named) {
    std::vector<GroundedModule> modules;
    modules.reserve(named.size());
    for (const NamedModule& item : named) {
        GroundedModule module{};
        module.base = item.base;
        module.size = item.size;
        module.kind = item.kind;
        modules.push_back(module);
    }
    return modules;
}

const NamedModule* module_containing(const std::vector<NamedModule>& modules, std::uint64_t address) {
    for (const NamedModule& module : modules) {
        if (module.size == 0) {
            continue;
        }
        if (address >= module.base && address < module.base + module.size) {
            return &module;
        }
    }
    return nullptr;
}

bool region_from_mbi(const MEMORY_BASIC_INFORMATION& info, GroundedRegion& region) {
    region = {};
    region.base = reinterpret_cast<std::uint64_t>(info.BaseAddress);
    region.size = static_cast<std::uint64_t>(info.RegionSize);
    region.state = info.State;
    region.protect = info.Protect;
    region.type = info.Type;
    return region.base != 0 && region.size != 0;
}

bool query_region(HANDLE process, std::uint64_t address, GroundedRegion& region) {
    MEMORY_BASIC_INFORMATION info{};
    const SIZE_T copied = VirtualQueryEx(
        process, reinterpret_cast<LPCVOID>(address), &info, sizeof(info));
    if (copied != sizeof(info)) {
        return false;
    }
    return region_from_mbi(info, region);
}

bool triple_pages_eligible(HANDLE process,
                           std::uint64_t address,
                           const std::vector<GroundedModule>& modules) {
    if (grounded_address_excluded(address, modules.data(), modules.size())) {
        return false;
    }
    GroundedRegion first{};
    if (!query_region(process, address, first) || !grounded_region_eligible(first)) {
        return false;
    }
    const std::uint64_t last = address + 11ull;
    if (last < first.base + first.size) {
        return true;
    }
    GroundedRegion second{};
    if (!query_region(process, last, second) || !grounded_region_eligible(second)) {
        return false;
    }
    return !grounded_address_excluded(last, modules.data(), modules.size());
}

bool read_exact(HANDLE process, std::uint64_t address, void* out, std::size_t bytes) {
    SIZE_T copied = 0;
    if (!ReadProcessMemory(process, reinterpret_cast<LPCVOID>(address), out, bytes, &copied)) {
        return false;
    }
    return copied == bytes;
}

bool read_live_triple(HANDLE process,
                      std::uint64_t address,
                      const std::vector<GroundedModule>& modules,
                      GroundedVec3& out) {
    if (!triple_pages_eligible(process, address, modules)) {
        return false;
    }
    std::uint8_t raw[12]{};
    if (!read_exact(process, address, raw, sizeof(raw))) {
        return false;
    }
    if (!decode_f32(raw, out.x) || !decode_f32(raw + 4, out.y) || !decode_f32(raw + 8, out.z)) {
        return false;
    }
    if (grounded_candidate_component_absurd(out.x) || grounded_candidate_component_absurd(out.y)
        || grounded_candidate_component_absurd(out.z)) {
        return false;
    }
    return true;
}

void clear_scan(ScanOutput& output, const char* reason) {
    output.aborted = true;
    output.abort_reason = reason;
    output.hits.clear();
    output.float64_hits.clear();
}

void publish_scan(const ScanOutput& output) {
    const GroundedScanTiming& timing = output.timing;
    std::printf(
        "  scan enumeration_ms=%llu eligible_region_count=%u eligible_bytes=%llu bytes_attempted=%llu bytes_read=%llu read_ms=%llu match_ms=%llu filter_ms=%llu total_scan_ms=%llu timeout_stage=%s timeout_budget_ms=%u partial_scan=%d\n",
        static_cast<unsigned long long>(timing.enumeration_ms),
        timing.eligible_region_count,
        static_cast<unsigned long long>(timing.eligible_bytes),
        static_cast<unsigned long long>(timing.bytes_attempted),
        static_cast<unsigned long long>(timing.bytes_read),
        static_cast<unsigned long long>(timing.read_ms),
        static_cast<unsigned long long>(timing.match_ms),
        static_cast<unsigned long long>(timing.filter_ms),
        static_cast<unsigned long long>(timing.total_scan_ms),
        timing.timeout_stage != nullptr ? timing.timeout_stage : kGroundedStageNone,
        timing.timeout_budget_ms,
        timing.partial_scan ? 1 : 0);
    std::fflush(stdout);
}

bool fail_scan(ScanOutput& output, const char* stage, std::uint64_t now) {
    output.timing.timeout_stage = stage;
    output.timing.timeout_budget_ms = kGroundedFullScanMs;
    output.timing.total_scan_ms = now - output.scan_started_ms;
    output.timing.eligible_bytes = output.bytes_considered;
    output.timing.partial_scan = output.timing.bytes_attempted > 0 || output.timing.bytes_read > 0
        || output.bytes_considered > 0;
    clear_scan(output, "timeout");
    publish_scan(output);
    return false;
}

bool range_overlaps_excluded(std::uint64_t begin,
                             std::uint64_t end,
                             const std::vector<GroundedModule>& modules) {
    if (end <= begin) {
        return false;
    }
    const std::uint64_t last = end - 1;
    for (const GroundedModule& module : modules) {
        if (!grounded_module_excludes(module.kind) || module.size == 0) {
            continue;
        }
        const std::uint64_t module_last = module.base + module.size - 1;
        if (begin <= module_last && last >= module.base) {
            return true;
        }
    }
    return false;
}

void note_page_counts(ScanOutput& output) {
    for (LiveHit& hit : output.hits) {
        std::uint32_t count = 0;
        const std::uint64_t page = hit.address / kGroundedPageBytes;
        for (const LiveHit& other : output.hits) {
            if (other.address / kGroundedPageBytes == page) {
                ++count;
            }
        }
        hit.initial_page_matches = count;
    }
}

#ifdef _MSC_VER
#pragma optimize("t", on)
#endif
bool scan_process(HANDLE process,
                  const std::vector<NamedModule>& named,
                  GroundedVec3 oracle,
                  ScanOutput& output) {
    output = {};
    output.scan_started_ms = GetTickCount64();
    output.timing.timeout_budget_ms = kGroundedFullScanMs;
    output.timing.timeout_stage = kGroundedStageNone;
    if (grounded_oracle_invalid(oracle)) {
        output.timing.total_scan_ms = GetTickCount64() - output.scan_started_ms;
        clear_scan(output, "oracle_invalid");
        publish_scan(output);
        return false;
    }
    const std::vector<GroundedModule> modules = as_grounded_modules(named);
    const std::uint64_t started = output.scan_started_ms;
    std::uint8_t* address = nullptr;
    std::vector<std::uint8_t> window;
    while (true) {
        const std::uint64_t query_began = GetTickCount64();
        if (query_began - started > kGroundedFullScanMs) {
            return fail_scan(output, kGroundedStageEnumeration, query_began);
        }
        MEMORY_BASIC_INFORMATION info{};
        const SIZE_T queried = VirtualQueryEx(process, address, &info, sizeof(info));
        output.timing.enumeration_ms += GetTickCount64() - query_began;
        if (queried != sizeof(info)) {
            break;
        }
        if (info.RegionSize == 0) {
            break;
        }
        GroundedRegion region{};
        const std::uint64_t next = reinterpret_cast<std::uint64_t>(info.BaseAddress)
            + static_cast<std::uint64_t>(info.RegionSize);
        address = reinterpret_cast<std::uint8_t*>(next);
        if (!region_from_mbi(info, region) || !grounded_region_eligible(region)) {
            continue;
        }
        if (region.size > kGroundedMaxBytesConsidered
            || output.bytes_considered > kGroundedMaxBytesConsidered - region.size) {
            output.timing.total_scan_ms = GetTickCount64() - started;
            output.timing.eligible_bytes = output.bytes_considered;
            output.timing.partial_scan = output.bytes_considered > 0 || output.timing.bytes_read > 0;
            clear_scan(output, "byte_budget");
            publish_scan(output);
            return false;
        }
        output.bytes_considered += region.size;
        ++output.timing.eligible_region_count;
        output.timing.eligible_bytes = output.bytes_considered;
        bool excluded_span = true;
        if (!modules.empty()) {
            for (std::uint64_t probe = region.base; probe < region.base + region.size;
                 probe += std::max<std::uint64_t>(region.size / 8ull, 1ull)) {
                if (!grounded_address_excluded(probe, modules.data(), modules.size())) {
                    excluded_span = false;
                    break;
                }
            }
            if (excluded_span
                && !grounded_address_excluded(region.base + region.size - 1, modules.data(), modules.size())) {
                excluded_span = false;
            }
        } else {
            excluded_span = false;
        }
        if (excluded_span) {
            continue;
        }

        bool marked = false;
        std::uint64_t cursor = (region.base + 3ull) & ~3ull;
        const std::uint64_t region_end = region.base + region.size;
        while (cursor < region_end) {
            const std::uint64_t chunk_end = std::min(region_end, cursor + kScanWindow);
            std::uint64_t read_end = std::min(region_end, chunk_end + 12ull);
            if (chunk_end == region_end && triple_pages_eligible(process, region_end > 12 ? region_end - 4 : region_end, modules)) {
                GroundedRegion tail{};
                if (query_region(process, region_end, tail) && grounded_region_eligible(tail)) {
                    read_end = region_end + 12ull;
                }
            }
            if (read_end <= cursor) {
                break;
            }
            const std::size_t bytes = static_cast<std::size_t>(read_end - cursor);
            window.resize(bytes);
            output.timing.bytes_attempted += bytes;
            const std::uint64_t read_began = GetTickCount64();
            if (read_began - started > kGroundedFullScanMs) {
                return fail_scan(output, kGroundedStageRead, read_began);
            }
            const bool got = read_exact(process, cursor, window.data(), bytes);
            const std::uint64_t read_done = GetTickCount64();
            output.timing.read_ms += read_done - read_began;
            if (read_done - started > kGroundedFullScanMs) {
                return fail_scan(output, kGroundedStageRead, read_done);
            }
            if (!got) {
                cursor = chunk_end;
                continue;
            }
            output.timing.bytes_read += bytes;
            const std::uint64_t match_began = read_done;
            if (!marked && region.type == kPrivate
                && grounded_buffer_contains_marker(window.data(), window.size())) {
                marked = true;
            }
            const bool check_excluded = range_overlaps_excluded(cursor, read_end, modules);
            for (std::uint64_t local = 0; cursor + local + 12ull <= read_end && cursor + local < chunk_end;
                 local += 4ull) {
                if (local != 0 && (local % kGroundedScanTimeSliceBytes) == 0) {
                    const std::uint64_t matched = GetTickCount64();
                    if (matched - started > kGroundedFullScanMs) {
                        output.timing.match_ms += matched - match_began;
                        return fail_scan(output, kGroundedStageMatch, matched);
                    }
                }
                const std::uint64_t absolute = cursor + local;
                if (check_excluded && grounded_address_excluded(absolute, modules.data(), modules.size())) {
                    continue;
                }
                if (absolute + 12ull > region_end) {
                    if (!triple_pages_eligible(process, absolute, modules)) {
                        continue;
                    }
                }
                GroundedVec3 value{};
                if (!decode_f32(window.data() + local, value.x)
                    || !decode_f32(window.data() + local + 4, value.y)
                    || !decode_f32(window.data() + local + 8, value.z)) {
                    continue;
                }
                if (grounded_candidate_component_absurd(value.x) || grounded_candidate_component_absurd(value.y)
                    || grounded_candidate_component_absurd(value.z)) {
                    continue;
                }
                GroundedVec3 offset{};
                if (!grounded_triple_can_match(value, oracle) || !grounded_select_offset(value, oracle, offset)) {
                    continue;
                }
                if (output.hits.size() >= kGroundedMaxCandidates) {
                    output.timing.match_ms += GetTickCount64() - match_began;
                    output.timing.total_scan_ms = GetTickCount64() - started;
                    output.timing.eligible_bytes = output.bytes_considered;
                    output.timing.partial_scan = true;
                    clear_scan(output, "candidate_cap");
                    publish_scan(output);
                    return false;
                }
                LiveHit hit{};
                hit.address = absolute;
                hit.region_base = region.base;
                hit.region_type = region.type;
                hit.x = value.x;
                hit.y = value.y;
                hit.z = value.z;
                hit.offset_x = offset.x;
                hit.offset_y = offset.y;
                hit.offset_z = offset.z;
                hit.slots[static_cast<int>(GroundedSlot::StandA1)] = {true, value.x, value.y, value.z};
                const NamedModule* host = module_containing(named, absolute);
                hit.module_name = host != nullptr ? host->name : "private";
                output.hits.push_back(std::move(hit));
            }
            if (window.size() >= 24) {
                for (std::uint64_t local = 0; cursor + local + 24ull <= read_end && cursor + local < chunk_end;
                     local += 8ull) {
                    const std::uint64_t absolute = cursor + local;
                    if (check_excluded && grounded_address_excluded(absolute, modules.data(), modules.size())) {
                        continue;
                    }
                    Float64Diagnostic diagnostic{};
                    if (!decode_f64(window.data() + local, diagnostic.x)
                        || !decode_f64(window.data() + local + 8, diagnostic.y)
                        || !decode_f64(window.data() + local + 16, diagnostic.z)) {
                        continue;
                    }
                    const GroundedVec3 wide_value{diagnostic.x, diagnostic.y, diagnostic.z};
                    GroundedVec3 offset{};
                    if (!grounded_triple_can_match(wide_value, oracle)
                        || !grounded_select_offset(wide_value, oracle, offset)) {
                        continue;
                    }
                    diagnostic.address = absolute;
                    output.float64_hits.push_back(diagnostic);
                }
            }
            output.timing.match_ms += GetTickCount64() - match_began;
            cursor = chunk_end;
        }
        if (marked) {
            for (LiveHit& hit : output.hits) {
                if (hit.region_base == region.base) {
                    hit.marker_allocation = true;
                }
            }
        }
        if ((output.bytes_considered / (256ull * 1024ull * 1024ull))
            != ((output.bytes_considered - region.size) / (256ull * 1024ull * 1024ull))) {
            std::printf("  scan considered %llu MiB, matches %zu\n",
                        static_cast<unsigned long long>(output.bytes_considered / (1024ull * 1024ull)),
                        output.hits.size());
            std::fflush(stdout);
        }
    }
    const std::uint64_t filter_began = GetTickCount64();
    if (filter_began - started > kGroundedFullScanMs) {
        return fail_scan(output, kGroundedStageFilter, filter_began);
    }
    note_page_counts(output);
    std::sort(output.hits.begin(), output.hits.end(), [](const LiveHit& left, const LiveHit& right) {
        return left.address < right.address;
    });
    const std::uint64_t finished = GetTickCount64();
    output.timing.filter_ms += finished - filter_began;
    output.timing.total_scan_ms = finished - started;
    output.timing.eligible_bytes = output.bytes_considered;
    output.timing.partial_scan = false;
    output.timing.timeout_stage = kGroundedStageNone;
    publish_scan(output);
    return true;
}
#ifdef _MSC_VER
#pragma optimize("", on)
#endif

bool identity_holds(std::uint32_t pid, std::uint64_t creation, const std::wstring& path) {
    std::wstring live_path;
    std::uint64_t live_creation = 0;
    if (!detail::query_process_identity(pid, live_path, live_creation)) {
        return false;
    }
    return grounded_process_identity_holds(
        pid, creation, detail::wide_to_utf8(path), pid, live_creation, detail::wide_to_utf8(live_path));
}

std::uint64_t file_size_of(const std::wstring& path) {
    WIN32_FILE_ATTRIBUTE_DATA info{};
    if (!GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &info)) {
        return 0;
    }
    ULARGE_INTEGER size{};
    size.LowPart = info.nFileSizeLow;
    size.HighPart = info.nFileSizeHigh;
    return size.QuadPart;
}

bool file_exists(const std::wstring& path) {
    const DWORD attributes = GetFileAttributesW(path.c_str());
    return attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_DIRECTORY) == 0;
}

std::wstring roaming_log_path() {
    wchar_t appdata[MAX_PATH]{};
    const DWORD length = GetEnvironmentVariableW(L"APPDATA", appdata, MAX_PATH);
    if (length == 0 || length >= MAX_PATH) {
        return {};
    }
    return std::wstring(appdata) + L"\\CLEO Redux\\cleo_redux.log";
}

std::wstring evidence_directory() {
    wchar_t local[MAX_PATH]{};
    const DWORD length = GetEnvironmentVariableW(L"LOCALAPPDATA", local, MAX_PATH);
    if (length == 0 || length >= MAX_PATH) {
        return {};
    }
    const std::wstring directory = std::wstring(local) + L"\\DeFlux\\research";
    CreateDirectoryW((std::wstring(local) + L"\\DeFlux").c_str(), nullptr);
    CreateDirectoryW(directory.c_str(), nullptr);
    return directory;
}

void say(const std::string& line) {
    std::printf("%s\n", line.c_str());
    std::fflush(stdout);
}

bool read_line(std::string& out) {
    char buffer[256]{};
    if (std::fgets(buffer, sizeof(buffer), stdin) == nullptr) {
        return false;
    }
    out = buffer;
    while (!out.empty() && (out.back() == '\n' || out.back() == '\r' || out.back() == ' ')) {
        out.pop_back();
    }
    return true;
}

bool confirm_on_foot(std::string& message) {
    say("Type ON FOOT and press Enter if CJ is on foot. Type VEHICLE if CJ is in a vehicle. Type ABORT to stop.");
    while (true) {
        std::string line;
        if (!read_line(line)) {
            message = "LIVE_EXPERIMENT_INVALID";
            return false;
        }
        if (line == "ABORT") {
            message = "LIVE_EXPERIMENT_INVALID";
            return false;
        }
        if (line == "VEHICLE") {
            say("Vehicle state invalidates this session.");
            message = "LIVE_EXPERIMENT_INVALID";
            return false;
        }
        if (line == "ON FOOT") {
            return true;
        }
        say("Type ON FOOT, VEHICLE, or ABORT.");
    }
}

struct OraclePair {
    OracleLogWatcher game;
    OracleLogWatcher roaming;
};

void poll_oracles(OraclePair& pair, const std::wstring& game_log, const std::wstring& roaming_log) {
    const std::uint64_t now = GetTickCount64();
    (void)pair.game.poll_file(game_log, now);
    if (!roaming_log.empty()) {
        (void)pair.roaming.poll_file(roaming_log, now);
    }
}

std::vector<OracleSample> phase_samples(const OraclePair& pair,
                                        std::string_view phase,
                                        std::uint64_t not_before) {
    std::vector<OracleSample> selected;
    auto take = [&](const std::vector<OracleSample>& samples) {
        for (const OracleSample& sample : samples) {
            if (sample.phase == phase && sample.observed_ms >= not_before) {
                selected.push_back(sample);
            }
        }
    };
    take(pair.game.samples());
    take(pair.roaming.samples());
    return selected;
}

bool wait_for_phase(OraclePair& pair,
                    const std::wstring& game_log,
                    const std::wstring& roaming_log,
                    std::string_view phase,
                    bool require_stable,
                    GroundedVec3& median,
                    std::string& message) {
    const std::uint64_t started = GetTickCount64();
    std::uint64_t last_notice = started;
    while (true) {
        poll_oracles(pair, game_log, roaming_log);
        const std::vector<OracleSample> samples = phase_samples(pair, phase, started);
        if (samples.size() >= 2) {
            const std::uint64_t first = samples.front().observed_ms;
            const std::uint64_t last = samples.back().observed_ms;
            if (grounded_phase_duration_ok(first, last)) {
                if (!grounded_standing_median(samples.data(), samples.size(), median)
                    || grounded_oracle_invalid(median)) {
                    message = "LIVE_EXPERIMENT_INVALID";
                    say("INVALID_PHASE. Oracle median is non-finite or absurd.");
                    return false;
                }
                if (!require_stable || grounded_standing_oracle_stable(samples.data(), samples.size())) {
                    return true;
                }
                message = "LIVE_EXPERIMENT_INVALID";
                say("INVALID_PHASE. Standing oracle samples disagree by more than 0.05 m.");
                return false;
            }
        }
        if ((GetAsyncKeyState(VK_ESCAPE) & 0x8000) != 0) {
            message = "LIVE_EXPERIMENT_INVALID";
            say("INVALID_PHASE. Escape pressed.");
            return false;
        }
        const std::uint64_t now = GetTickCount64();
        if (now - last_notice >= 2000) {
            std::printf("  waiting for %.*s, samples %zu, elapsed %llu ms\n",
                        static_cast<int>(phase.size()),
                        phase.data(),
                        samples.size(),
                        static_cast<unsigned long long>(now - started));
            std::fflush(stdout);
            last_notice = now;
        }
        if (std::feof(stdin)) {
            message = "LIVE_EXPERIMENT_INVALID";
            return false;
        }
        Sleep(kGroundedWatcherPollMs);
    }
}

const char* region_type_name(std::uint32_t type) {
    if (type == 0x20000u) {
        return "MEM_PRIVATE";
    }
    if (type == 0x1000000u) {
        return "MEM_IMAGE";
    }
    return "OTHER";
}

const char* match_class_name(const LiveHit& hit, GroundedVec3 oracle) {
    const GroundedVec3 memory{hit.x, hit.y, hit.z};
    const GroundedVec3 shifted{oracle.x + hit.offset_x, oracle.y + hit.offset_y, oracle.z + hit.offset_z};
    if (hit.offset_x != 0.0 || hit.offset_y != 0.0 || hit.offset_z != 0.0) {
        return "FIXED_OFFSET_MATCH";
    }
    if (grounded_exact_match(memory, shifted)) {
        return "EXACT_MATCH";
    }
    return "NEAR_MATCH";
}

GroundedObservation observation_from_hit(const LiveHit& hit,
                                         GroundedVec3 oracle_a,
                                         GroundedVec3 oracle_c,
                                         GroundedVec3 oracle_e,
                                         GroundedVec3 oracle_h) {
    GroundedObservation observation{};
    observation.address = hit.address;
    observation.offset_x = hit.offset_x;
    observation.offset_y = hit.offset_y;
    observation.offset_z = hit.offset_z;
    observation.has_offset = true;
    observation.marker_allocation = hit.marker_allocation;
    observation.initial_page_matches = hit.initial_page_matches;
    observation.qualified_page_matches = 1;
    observation.oracle_a = oracle_a;
    observation.oracle_c = oracle_c;
    observation.oracle_e = oracle_e;
    observation.oracle_h = oracle_h;
    for (std::size_t slot = 0; slot < static_cast<std::size_t>(GroundedSlot::Count); ++slot) {
        observation.slots[slot] = hit.slots[slot];
    }
    return observation;
}

void assign_final_classes(std::vector<LiveHit>& hits,
                          GroundedVec3 oracle_a,
                          GroundedVec3 oracle_c,
                          GroundedVec3 oracle_e,
                          GroundedVec3 oracle_h) {
    std::vector<std::uint64_t> strong_on_page;
    strong_on_page.reserve(hits.size());
    for (const LiveHit& hit : hits) {
        if (!hit.alive) {
            continue;
        }
        GroundedObservation observation = observation_from_hit(hit, oracle_a, oracle_c, oracle_e, oracle_h);
        if (classify_grounded_observation(observation).evidence == GroundedEvidence::Strong) {
            strong_on_page.push_back(hit.address / kGroundedPageBytes);
        }
    }
    for (LiveHit& hit : hits) {
        if (!hit.alive) {
            continue;
        }
        GroundedObservation observation = observation_from_hit(hit, oracle_a, oracle_c, oracle_e, oracle_h);
        if (hit.initial_page_matches > kGroundedDensePageInitial) {
            std::uint32_t on_page = 0;
            const std::uint64_t page = hit.address / kGroundedPageBytes;
            for (std::uint64_t other : strong_on_page) {
                if (other == page) {
                    ++on_page;
                }
            }
            observation.qualified_page_matches = on_page == 0 ? 1u : on_page;
        }
        const GroundedClassification classification = classify_grounded_observation(observation);
        hit.evidence = classification.evidence;
        hit.reason = classification.reason;
        hit.qualified_page_matches = observation.qualified_page_matches;
        if (hit.evidence != GroundedEvidence::Strong) {
            hit.alive = false;
        }
    }
}

bool fill_slot(HANDLE process,
               const std::vector<GroundedModule>& modules,
               LiveHit& hit,
               GroundedSlot slot) {
    GroundedVec3 value{};
    SlotRead& read = hit.slots[static_cast<int>(slot)];
    read = {};
    if (!read_live_triple(process, hit.address, modules, value)) {
        return false;
    }
    read.present = true;
    read.x = value.x;
    read.y = value.y;
    read.z = value.z;
    return true;
}

bool file_size_of_image(const std::wstring& path, std::uint32_t& size_of_image) {
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        return false;
    }
    std::vector<char> headers(0x1000);
    input.read(headers.data(), static_cast<std::streamsize>(headers.size()));
    const auto count = input.gcount();
    if (count <= 0) {
        return false;
    }
    PeImageInfo info{};
    if (parse_pe64_headers(reinterpret_cast<const std::uint8_t*>(headers.data()),
                           static_cast<std::size_t>(count),
                           info)
        != PeParseStatus::Ok) {
        return false;
    }
    size_of_image = info.size_of_image;
    return size_of_image != 0;
}

bool write_medians(const std::wstring& path,
                   GroundedVec3 a,
                   GroundedVec3 c,
                   GroundedVec3 e,
                   GroundedVec3 h,
                   std::size_t strong_count) {
    FILE* file = nullptr;
    if (_wfopen_s(&file, path.c_str(), L"wb") != 0 || file == nullptr) {
        return false;
    }
    std::fprintf(file, "STRONG %zu\n", strong_count);
    std::fprintf(file, "A %.6f %.6f %.6f\n", a.x, a.y, a.z);
    std::fprintf(file, "C %.6f %.6f %.6f\n", c.x, c.y, c.z);
    std::fprintf(file, "E %.6f %.6f %.6f\n", e.x, e.y, e.z);
    std::fprintf(file, "H %.6f %.6f %.6f\n", h.x, h.y, h.z);
    std::fclose(file);
    return true;
}

bool read_medians(const std::wstring& path,
                  GroundedVec3& a,
                  GroundedVec3& c,
                  GroundedVec3& e,
                  GroundedVec3& h,
                  std::size_t& strong_count) {
    std::ifstream input(path);
    if (!input) {
        return false;
    }
    std::string kind;
    if (!(input >> kind >> strong_count) || kind != "STRONG") {
        return false;
    }
    auto read_vec = [&](const char* label, GroundedVec3& value) {
        std::string got;
        return static_cast<bool>(input >> got >> value.x >> value.y >> value.z) && got == label;
    };
    return read_vec("A", a) && read_vec("C", c) && read_vec("E", e) && read_vec("H", h);
}

} // namespace

std::uint32_t grounded_live_process_access() noexcept {
    return static_cast<std::uint32_t>(kLiveAccess);
}

bool grounded_live_access_is_read_only(std::uint32_t access) noexcept {
    constexpr std::uint32_t kWrite = 0x0020u;
    constexpr std::uint32_t kOperation = 0x0008u;
    constexpr std::uint32_t kThread = 0x0002u;
    if ((access & (kWrite | kOperation | kThread)) != 0) {
        return false;
    }
    return access == grounded_live_process_access();
}

bool grounded_asi_loader_sha_is_official(std::string_view sha256_hex) noexcept {
    return equals_ignore_case(sha256_hex, kOfficialAsiLoaderSha256);
}

GroundedLiveBlock evaluate_grounded_live_gate(const GroundedLiveGateInput& input) noexcept {
    if (!input.explicit_intent) {
        return GroundedLiveBlock::IntentMissing;
    }
    if (input.isolation_engaged) {
        return GroundedLiveBlock::IsolationEngaged;
    }
    if (!grounded_live_access_is_read_only(input.requested_access)) {
        return GroundedLiveBlock::WriteAccessRejected;
    }
    if (!input.size_matches) {
        return GroundedLiveBlock::BuildSizeMismatch;
    }
    if (!input.sha_matches) {
        return GroundedLiveBlock::BuildShaMismatch;
    }
    if (!input.version_matches) {
        return GroundedLiveBlock::BuildVersionMismatch;
    }
    if (!input.process_present) {
        return GroundedLiveBlock::TargetAbsent;
    }
    if (!input.path_matches) {
        return GroundedLiveBlock::ProcessPathMismatch;
    }
    if (!input.creation_present) {
        return GroundedLiveBlock::CreationMissing;
    }
    if (!input.module_identity_matches) {
        return GroundedLiveBlock::ModuleIdentityMismatch;
    }
    return GroundedLiveBlock::Ready;
}

const char* grounded_live_block_message(GroundedLiveBlock block) noexcept {
    return block_text(block);
}

const char* grounded_live_result_token(bool first_process,
                                       std::size_t strong_count,
                                       bool cleo_absent_control_passed) noexcept {
    if (first_process) {
        return strong_count >= 1 ? "FIRST_PROCESS_CANDIDATE_ONLY" : kGroundedNoCandidate;
    }
    if (grounded_live_requirements_met(strong_count, cleo_absent_control_passed)) {
        return "TRANSFORM_CANDIDATE_ESTABLISHED";
    }
    return kGroundedNoCandidate;
}

int run_grounded_live_console(bool cleo_absent, std::string& message_out) {
    message_out.clear();
    const GtaSaDeAdapter adapter;
    const DetectionSnapshot detection = adapter.detect();
    if (detection.installation != InstallationStatus::Found || detection.executable_path.empty()) {
        message_out = "ABORT. GTA SA DE installation was not found.";
        say(message_out);
        return 2;
    }
    const std::wstring executable = detail::utf8_to_wide(detection.executable_path);
    const detail::FileFingerprint fingerprint = detail::fingerprint_executable(executable);
    std::uint32_t size_of_image = 0;
    const bool image_size_ok = file_size_of_image(executable, size_of_image);
    const auto slash = executable.find_last_of(L"\\/");
    const std::wstring directory = slash == std::wstring::npos ? std::wstring{} : executable.substr(0, slash);
    const std::wstring loader = directory + L"\\version.dll";
    const std::wstring oracle_script = directory + L"\\CLEO\\deflux14_grounded_oracle.js";
    const std::wstring game_log = directory + L"\\cleo_redux.log";
    const std::wstring roaming_log = roaming_log_path();
    const EnvironmentPreflightResult preflight = scan_game_executable_directory(detection.executable_path);

    GroundedLiveGateInput gate{};
    gate.explicit_intent = true;
    gate.isolation_engaged = false;
    gate.requested_access = grounded_live_process_access();
    gate.size_matches = fingerprint.ok && fingerprint.size_bytes == kGroundedExpectedFileSize;
    gate.sha_matches = fingerprint.ok && is_research_allowlisted_sha256(fingerprint.sha256_hex)
        && !is_production_supported_sha256(fingerprint.sha256_hex);
    gate.version_matches = fingerprint.file_version == kGroundedExpectedVersion;

    if (!gate.size_matches || !gate.sha_matches || !gate.version_matches) {
        const GroundedLiveBlock block = evaluate_grounded_live_gate(gate);
        message_out = grounded_live_block_message(block);
        say(message_out);
        return 2;
    }
    if (!preflight.clean) {
        message_out = "ABORT. " + preflight.message;
        say(message_out);
        return 2;
    }
    std::string loader_sha;
    std::uint64_t loader_size = 0;
    if (!detail::sha256_file(loader, loader_sha, loader_size) || !grounded_asi_loader_sha_is_official(loader_sha)) {
        message_out = "ABORT. Official Ultimate ASI Loader version.dll is not installed.";
        say(message_out);
        return 2;
    }
    if (cleo_absent && file_exists(oracle_script)) {
        message_out = "ABORT. Disable only deflux14_grounded_oracle.js before the CLEO-absent control.";
        say(message_out);
        return 2;
    }
    if (!cleo_absent && !file_exists(oracle_script)) {
        message_out = "ABORT. The research oracle script is not installed in CLEO.";
        say(message_out);
        return 2;
    }

    const detail::VerifiedProcess verified = detail::query_verified_process(executable);
    gate.process_present = verified.status == detail::VerifiedQueryStatus::Found;
    gate.path_matches = gate.process_present && detail::executable_paths_equal(verified.image_path, executable);
    gate.creation_present = gate.process_present && verified.creation_identity != 0;
    gate.module_identity_matches = false;
    detail::MainModule module{};
    if (gate.process_present && gate.path_matches && gate.creation_present) {
        module = detail::query_main_module(verified.process_id, executable);
        gate.module_identity_matches = module.found && image_size_ok
            && module.size == static_cast<std::uint64_t>(size_of_image)
            && detail::executable_paths_equal(module.path, executable);
    }
    const GroundedLiveBlock decision = evaluate_grounded_live_gate(gate);
    if (decision != GroundedLiveBlock::Ready) {
        message_out = grounded_live_block_message(decision);
        say(message_out);
        return 2;
    }

    say("READ-ONLY RESEARCH SESSION");
    say("TARGET BUILD VERIFIED");
    say("TASK #14C");
    say("NO MEMORY WRITES");
    std::printf("  pid %u\n", verified.process_id);
    std::printf("  creation %llu\n", static_cast<unsigned long long>(verified.creation_identity));
    std::printf("  module base 0x%llx size 0x%llx\n",
                static_cast<unsigned long long>(module.base),
                static_cast<unsigned long long>(module.size));
    std::fflush(stdout);

    ResearchIntentSession intent{};
    ResearchIntentPublishRequest intent_request{};
    intent_request.target.process_id = verified.process_id;
    intent_request.target.creation_identity = verified.creation_identity;
    intent_request.target.executable_path = detection.executable_path;
    intent_request.expected_sha256 = kResearchAllowlistedSha256;
    intent_request.experiment_id = kGroundedTransformExperimentId;
    intent_request.synthetic_fixture = false;
    std::string intent_message;
    if (!publish_research_intent_session(intent, intent_request, intent_message)) {
        message_out = "ABORT. " + intent_message;
        say(message_out);
        return 2;
    }
    (void)mark_research_intent_consumed(intent_message);
    const std::uint64_t nonce = intent.view != nullptr ? intent.view->session_nonce : 0;

    HANDLE process = OpenProcess(static_cast<DWORD>(grounded_live_process_access()), FALSE, verified.process_id);
    if (process == nullptr) {
        close_research_intent_session(intent);
        message_out = "ABORT. OpenProcess(QUERY_LIMITED|VM_READ) failed.";
        say(message_out);
        return 2;
    }

    std::vector<NamedModule> named;
    if (!query_named_modules(verified.process_id, named)) {
        CloseHandle(process);
        close_research_intent_session(intent);
        message_out = "ABORT. Module enumeration failed.";
        say(message_out);
        return 2;
    }
    const std::vector<GroundedModule> modules = as_grounded_modules(named);

    const std::wstring evidence_dir = evidence_directory();
    const std::wstring medians_path = evidence_dir + L"\\grounded_14c_medians.txt";
    say("Oracle log: " + detail::wide_to_utf8(game_log));
    say("F8 arms or disarms CLEO. F9 advances one phase. Stay on foot.");

    auto finish = [&](int code, const std::string& message) {
        message_out = message;
        say(message_out);
        CloseHandle(process);
        close_research_intent_session(intent);
        return code;
    };

    if (!identity_holds(verified.process_id, verified.creation_identity, executable)) {
        return finish(3, "LIVE_EXPERIMENT_INVALID");
    }

    if (cleo_absent) {
        GroundedVec3 saved_a{};
        GroundedVec3 saved_c{};
        GroundedVec3 saved_e{};
        GroundedVec3 saved_h{};
        std::size_t saved_strong = 0;
        if (!read_medians(medians_path, saved_a, saved_c, saved_e, saved_h, saved_strong)
            || !grounded_standing_positions_separated(saved_a, saved_c, saved_e, saved_h)) {
            return finish(3, "LIVE_EXPERIMENT_INVALID");
        }
        OraclePair pair{};
        pair.game.start_after_existing(file_size_of(game_log));
        pair.roaming.start_after_existing(file_size_of(roaming_log));
        say("CLEO-absent control. The oracle script must stay disabled. Do not press F8.");
        say("Stand away from the four saved positions, type ON FOOT, then follow the revisit prompts.");
        if (!confirm_on_foot(message_out)) {
            return finish(3, message_out);
        }
        const std::uint64_t quiet_started = GetTickCount64();
        while (GetTickCount64() - quiet_started < 3000) {
            poll_oracles(pair, game_log, roaming_log);
            Sleep(kGroundedWatcherPollMs);
        }
        if (pair.game.accepted() != 0 || pair.roaming.accepted() != 0) {
            say("DEFLUX14 records appeared while the oracle script was supposed to be absent.");
            return finish(3, "LIVE_EXPERIMENT_INVALID");
        }
        const GroundedVec3 saved[4] = {saved_a, saved_c, saved_e, saved_h};
        const char* labels[4] = {"A", "C", "E", "H"};
        ScanOutput current{};
        bool control_passed = false;
        for (int index = 0; index < 4; ++index) {
            std::printf("Return to saved standing position %s and stand still. Press Enter when still.\n", labels[index]);
            std::fflush(stdout);
            std::string line;
            if (!read_line(line) || line == "ABORT") {
                return finish(3, "LIVE_EXPERIMENT_INVALID");
            }
            if (!confirm_on_foot(message_out)) {
                return finish(3, message_out);
            }
            if (!identity_holds(verified.process_id, verified.creation_identity, executable)) {
                return finish(3, "LIVE_EXPERIMENT_INVALID");
            }
            if (index == 0) {
                if (!scan_process(process, named, saved[index], current)) {
                    say(current.abort_reason);
                    return finish(3, "LIVE_EXPERIMENT_INVALID");
                }
            } else {
                for (LiveHit& hit : current.hits) {
                    if (!hit.alive) {
                        continue;
                    }
                    GroundedVec3 value{};
                    if (!read_live_triple(process, hit.address, modules, value)
                        || !grounded_offset_holds(
                            value, saved[index], GroundedVec3{hit.offset_x, hit.offset_y, hit.offset_z})) {
                        hit.alive = false;
                    }
                }
            }
            poll_oracles(pair, game_log, roaming_log);
            if (pair.game.accepted() != 0 || pair.roaming.accepted() != 0) {
                return finish(3, "LIVE_EXPERIMENT_INVALID");
            }
        }
        std::size_t survivors = 0;
        for (const LiveHit& hit : current.hits) {
            if (!hit.alive || hit.marker_allocation) {
                continue;
            }
            const NamedModule* host = module_containing(named, hit.address);
            if (host != nullptr && grounded_module_excludes(host->kind)) {
                continue;
            }
            ++survivors;
        }
        control_passed = survivors >= 1 && saved_strong >= 1;
        const char* token = grounded_live_result_token(false, saved_strong, control_passed);
        std::printf("CLEO-absent survivors %zu\n", survivors);
        std::printf("session_nonce %llu\n", static_cast<unsigned long long>(nonce));
        return finish(0, token);
    }

    say("A_STAND: stand completely still, press F8 to ARM, and remain still.");
    if (!confirm_on_foot(message_out)) {
        return finish(3, message_out);
    }
    OraclePair pair{};
    pair.game.start_after_existing(file_size_of(game_log));
    pair.roaming.start_after_existing(file_size_of(roaming_log));
    GroundedVec3 oracle_a{};
    if (!wait_for_phase(pair, game_log, roaming_log, "A_STAND", true, oracle_a, message_out)) {
        return finish(3, message_out);
    }
    // The scan budget starts inside scan_process. Time spent in wait_for_phase does not reduce it.
    say("A_STAND oracle is stable. Full scan is running. Remain still.");
    if (!identity_holds(verified.process_id, verified.creation_identity, executable)) {
        return finish(3, "LIVE_EXPERIMENT_INVALID");
    }
    ScanOutput scanned{};
    if (!scan_process(process, named, oracle_a, scanned)) {
        say(scanned.abort_reason != nullptr ? scanned.abort_reason : "scan_failed");
        return finish(3, "LIVE_EXPERIMENT_INVALID");
    }
    std::printf("phase A matches %zu\n", scanned.hits.size());
    std::fflush(stdout);
    if (scanned.hits.empty()) {
        const char* token = grounded_live_result_token(true, 0, false);
        return finish(0, token);
    }

    // Post-scan double-read starts a new oracle wait. It does not use the scan budget.
    say("Standing double-read in about 1 second. Remain still.");
    Sleep(1000);
    GroundedVec3 oracle_a_check{};
    if (!wait_for_phase(pair, game_log, roaming_log, "A_STAND", true, oracle_a_check, message_out)) {
        return finish(3, message_out);
    }
    for (LiveHit& hit : scanned.hits) {
        fill_slot(process, modules, hit, GroundedSlot::StandA2);
    }

    const struct Prompt {
        const char* phase;
        const char* text;
        bool standing;
        GroundedSlot slot;
        GroundedSlot second;
    } prompts[] = {
        {"B_WALK", "B_WALK: press F9 once, then walk at least 15 m. Stay on foot.", false, GroundedSlot::Count, GroundedSlot::Count},
        {"C_STAND", "C_STAND: stop completely and remain still.", true, GroundedSlot::StandC1, GroundedSlot::StandC2},
        {"D_MOVE", "D_MOVE: press F9 once, then move at least 15 m from every previous standing position.", false, GroundedSlot::Count, GroundedSlot::Count},
        {"E_STAND", "E_STAND: stop completely and remain still.", true, GroundedSlot::StandE1, GroundedSlot::StandE2},
        {"F_TURN", "F_TURN: press F9 once. Stay in place and rotate CJ about 90 to 180 degrees.", false, GroundedSlot::TurnF, GroundedSlot::Count},
        {"G_RUN", "G_RUN: press F9 once, then run at least 15 m from every previous standing position.", false, GroundedSlot::Count, GroundedSlot::Count},
        {"H_STAND", "H_STAND: stop completely and remain still.", true, GroundedSlot::StandH1, GroundedSlot::StandH2},
    };

    GroundedVec3 oracle_c{};
    GroundedVec3 oracle_e{};
    GroundedVec3 oracle_h{};
    bool have_c = false;
    bool have_e = false;
    bool have_h = false;
    for (const Prompt& prompt : prompts) {
        say(prompt.text);
        if (!confirm_on_foot(message_out)) {
            return finish(3, message_out);
        }
        GroundedVec3 median{};
        if (!wait_for_phase(pair, game_log, roaming_log, prompt.phase, prompt.standing, median, message_out)) {
            return finish(3, message_out);
        }
        if (prompt.phase == std::string("C_STAND")) {
            if (grounded_distance(oracle_a, median) < kGroundedMinSeparationMeters) {
                say("INVALID_PHASE. C_STAND is closer than 15 m to A_STAND.");
                return finish(3, "LIVE_EXPERIMENT_INVALID");
            }
            oracle_c = median;
            have_c = true;
        } else if (prompt.phase == std::string("E_STAND")) {
            if (grounded_distance(oracle_a, median) < kGroundedMinSeparationMeters
                || grounded_distance(oracle_c, median) < kGroundedMinSeparationMeters) {
                say("INVALID_PHASE. E_STAND is closer than 15 m to an earlier standing position.");
                return finish(3, "LIVE_EXPERIMENT_INVALID");
            }
            oracle_e = median;
            have_e = true;
        } else if (prompt.phase == std::string("H_STAND")) {
            if (!grounded_standing_positions_separated(oracle_a, oracle_c, oracle_e, median)) {
                say("INVALID_PHASE. H_STAND does not keep every standing pair at least 15 m apart.");
                return finish(3, "LIVE_EXPERIMENT_INVALID");
            }
            oracle_h = median;
            have_h = true;
        } else if (prompt.phase == std::string("F_TURN")) {
            if (!have_e || grounded_distance(oracle_e, median) > kGroundedTurnMeters) {
                say("INVALID_PHASE. F_TURN oracle translation is above 0.25 m.");
                return finish(3, "LIVE_EXPERIMENT_INVALID");
            }
        }
        if (prompt.slot == GroundedSlot::Count) {
            std::printf("  %s duration gate satisfied\n", prompt.phase);
            std::fflush(stdout);
            continue;
        }
        if (!identity_holds(verified.process_id, verified.creation_identity, executable)) {
            return finish(3, "LIVE_EXPERIMENT_INVALID");
        }
        for (LiveHit& hit : scanned.hits) {
            if (!hit.alive) {
                continue;
            }
            fill_slot(process, modules, hit, prompt.slot);
            const SlotRead& read = hit.slots[static_cast<int>(prompt.slot)];
            const GroundedVec3 offset{hit.offset_x, hit.offset_y, hit.offset_z};
            const GroundedVec3 older_a = oracle_a;
            const GroundedVec3 older_stack[3] = {oracle_a, oracle_c, oracle_e};
            std::size_t older_count = 0;
            const GroundedVec3* older = nullptr;
            GroundedVec3 current = median;
            if (prompt.slot == GroundedSlot::StandC1) {
                older = &older_a;
                older_count = 1;
                current = oracle_c;
            } else if (prompt.slot == GroundedSlot::StandE1) {
                older = older_stack;
                older_count = 2;
                current = oracle_e;
            } else if (prompt.slot == GroundedSlot::StandH1) {
                older = older_stack;
                older_count = 3;
                current = oracle_h;
            }
            if (!read.present) {
                hit.alive = false;
                hit.evidence = GroundedEvidence::Rejected;
                hit.reason = "unreadable";
                continue;
            }
            const GroundedVec3 memory{read.x, read.y, read.z};
            if (older != nullptr && grounded_is_stale(memory, current, older, older_count, offset)) {
                hit.alive = false;
                hit.evidence = GroundedEvidence::StaleCopy;
                hit.reason = "stale_copy";
                continue;
            }
            if (prompt.slot != GroundedSlot::TurnF && !grounded_offset_holds(memory, current, offset)) {
                hit.alive = false;
                hit.evidence = GroundedEvidence::Rejected;
                hit.reason = "offset_lost";
                continue;
            }
            if (prompt.slot == GroundedSlot::TurnF) {
                const SlotRead& stand_e = hit.slots[static_cast<int>(GroundedSlot::StandE1)];
                if (!stand_e.present
                    || !grounded_turn_stable(GroundedVec3{stand_e.x, stand_e.y, stand_e.z}, memory)) {
                    hit.alive = false;
                    hit.evidence = GroundedEvidence::Rejected;
                    hit.reason = "turn";
                }
            }
        }
        if (prompt.second != GroundedSlot::Count) {
            say("Standing double-read in about 1 second. Remain still.");
            Sleep(1000);
            for (LiveHit& hit : scanned.hits) {
                if (!hit.alive) {
                    continue;
                }
                fill_slot(process, modules, hit, prompt.second);
                const SlotRead& first = hit.slots[static_cast<int>(prompt.slot)];
                const SlotRead& second = hit.slots[static_cast<int>(prompt.second)];
                if (!second.present
                    || !grounded_double_read_stable(
                        GroundedVec3{first.x, first.y, first.z},
                        GroundedVec3{second.x, second.y, second.z})) {
                    hit.alive = false;
                    hit.evidence = GroundedEvidence::Rejected;
                    hit.reason = "double_read";
                }
            }
        }
        std::size_t alive = 0;
        for (const LiveHit& hit : scanned.hits) {
            if (hit.alive) {
                ++alive;
            }
        }
        std::printf("  survivors after %s: %zu\n", prompt.phase, alive);
        std::fflush(stdout);
    }

    say("Silence control: stay still and press F8 to DISARM. Do not move CJ.");
    if (!confirm_on_foot(message_out)) {
        return finish(3, message_out);
    }
    const std::uint32_t accepted_before = pair.game.accepted() + pair.roaming.accepted();
    const std::uint64_t silence_started = GetTickCount64();
    bool quiet = false;
    while (GetTickCount64() - silence_started < 15000) {
        poll_oracles(pair, game_log, roaming_log);
        const std::uint32_t accepted_now = pair.game.accepted() + pair.roaming.accepted();
        if (accepted_now == accepted_before && GetTickCount64() - silence_started >= 1000) {
            quiet = true;
            break;
        }
        Sleep(kGroundedWatcherPollMs);
    }
    if (!quiet) {
        say("INVALID_PHASE. CLEO was still emitting oracle samples.");
        return finish(3, "LIVE_EXPERIMENT_INVALID");
    }
    for (LiveHit& hit : scanned.hits) {
        if (!hit.alive) {
            continue;
        }
        const bool readable = fill_slot(process, modules, hit, GroundedSlot::Silence);
        const SlotRead& held = hit.slots[static_cast<int>(GroundedSlot::StandH1)];
        const SlotRead& silence = hit.slots[static_cast<int>(GroundedSlot::Silence)];
        if (!readable || !silence.present
            || !grounded_double_read_stable(
                GroundedVec3{held.x, held.y, held.z},
                GroundedVec3{silence.x, silence.y, silence.z})) {
            hit.alive = false;
            hit.evidence = GroundedEvidence::CleoContamination;
            hit.reason = "silence_disappeared";
        }
    }

    for (LiveHit& hit : scanned.hits) {
        if (!hit.marker_allocation) {
            continue;
        }
        hit.alive = false;
        hit.evidence = GroundedEvidence::CleoContamination;
        hit.reason = "marker_allocation";
    }
    assign_final_classes(scanned.hits, oracle_a, oracle_c, oracle_e, oracle_h);

    std::size_t strong = 0;
    std::size_t contamination = 0;
    std::size_t stale = 0;
    std::size_t dense = 0;
    std::size_t pages = 0;
    std::uint64_t last_page = ~0ull;
    for (const LiveHit& hit : scanned.hits) {
        const std::uint64_t page = hit.address / kGroundedPageBytes;
        if (page != last_page) {
            ++pages;
            last_page = page;
        }
        if (hit.initial_page_matches > kGroundedDensePageInitial) {
            ++dense;
        }
        if (hit.evidence == GroundedEvidence::Strong) {
            ++strong;
        } else if (hit.evidence == GroundedEvidence::CleoContamination) {
            ++contamination;
        } else if (hit.evidence == GroundedEvidence::StaleCopy) {
            ++stale;
        }
    }
    std::printf("initial_matches %zu\n", scanned.hits.size());
    std::printf("pages %zu\n", pages);
    std::printf("contamination %zu\n", contamination);
    std::printf("stale %zu\n", stale);
    std::printf("dense_page_candidates %zu\n", dense);
    std::printf("strong %zu\n", strong);
    std::printf("float64_diagnostics %zu\n", scanned.float64_hits.size());
    std::printf("session_nonce %llu\n", static_cast<unsigned long long>(nonce));
    for (const LiveHit& hit : scanned.hits) {
        if (hit.evidence != GroundedEvidence::Strong && hit.evidence != GroundedEvidence::Moderate
            && hit.evidence != GroundedEvidence::StaleCopy && hit.evidence != GroundedEvidence::CleoContamination) {
            continue;
        }
        std::printf("address 0x%llx evidence %s reason %s region %s module %s match %s offset %.2f %.2f %.2f\n",
                    static_cast<unsigned long long>(hit.address),
                    to_string(hit.evidence),
                    hit.reason != nullptr ? hit.reason : "",
                    region_type_name(hit.region_type),
                    hit.module_name.c_str(),
                    match_class_name(hit, oracle_a),
                    hit.offset_x,
                    hit.offset_y,
                    hit.offset_z);
    }
    std::fflush(stdout);
    if (have_h) {
        (void)write_medians(medians_path, oracle_a, oracle_c, oracle_e, oracle_h, strong);
    }
    const char* token = grounded_live_result_token(true, strong, false);
    return finish(0, token);
}

} // namespace deflux::game::gta_sa_de
