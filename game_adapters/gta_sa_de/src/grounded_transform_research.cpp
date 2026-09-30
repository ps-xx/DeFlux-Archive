#include "deflux/game/gta_sa_de/grounded_transform_research.hpp"

#include "deflux/core/process_lifecycle.hpp"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstring>
#include <cstdlib>
#include <system_error>

namespace deflux::game::gta_sa_de {
namespace {

constexpr std::uint32_t kCommit = 0x1000u;
constexpr std::uint32_t kPrivate = 0x20000u;
constexpr std::uint32_t kMapped = 0x40000u;
constexpr std::uint32_t kImage = 0x1000000u;
constexpr std::uint32_t kNoAccess = 0x01u;
constexpr std::uint32_t kReadOnly = 0x02u;
constexpr std::uint32_t kReadWrite = 0x04u;
constexpr std::uint32_t kWriteCopy = 0x08u;
constexpr std::uint32_t kGuard = 0x100u;

const char* kPhases[] = {
    "A_STAND",
    "B_WALK",
    "C_STAND",
    "D_MOVE",
    "E_STAND",
    "F_TURN",
    "G_RUN",
    "H_STAND",
};

const char kMarkerAscii[] = "DEFLUX14";

bool readable_access(std::uint32_t protect) noexcept {
    const std::uint32_t access = protect & 0xFFu;
    return access == kReadOnly || access == kReadWrite || access == kWriteCopy;
}

bool executable_access(std::uint32_t protect) noexcept {
    return (protect & 0xF0u) != 0;
}

std::uint64_t clock_now(const ScanClock& clock) noexcept {
    if (clock.now_ms != nullptr) {
        return clock.now_ms(clock.context);
    }
    return grounded_observation_tick();
}

GroundedVec3 add_offset(GroundedVec3 oracle, GroundedVec3 offset) noexcept {
    return {oracle.x + offset.x, oracle.y + offset.y, oracle.z + offset.z};
}

const GroundedRegion* region_at(const GroundedView& view, std::uint64_t address) noexcept {
    for (std::size_t index = 0; index < view.region_count; ++index) {
        const GroundedRegion& region = view.regions[index];
        if (address >= region.base && address - region.base < region.size) {
            return &region;
        }
    }
    return nullptr;
}

bool region_fully_excluded(const GroundedRegion& region,
                           const GroundedModule* modules,
                           std::size_t module_count) noexcept {
    if (region.size == 0) {
        return false;
    }
    const std::uint64_t last = region.base + region.size - 1;
    for (std::size_t index = 0; index < module_count; ++index) {
        const GroundedModule& module = modules[index];
        if (!grounded_module_excludes(module.kind) || module.size == 0) {
            continue;
        }
        const std::uint64_t module_last = module.base + module.size - 1;
        if (region.base >= module.base && last <= module_last) {
            return true;
        }
    }
    return false;
}

bool region_overlaps_excluded(const GroundedRegion& region,
                              const GroundedModule* modules,
                              std::size_t module_count) noexcept {
    if (modules == nullptr || region.size == 0) {
        return false;
    }
    const std::uint64_t last = region.base + region.size - 1;
    for (std::size_t index = 0; index < module_count; ++index) {
        const GroundedModule& module = modules[index];
        if (!grounded_module_excludes(module.kind) || module.size == 0) {
            continue;
        }
        const std::uint64_t module_last = module.base + module.size - 1;
        if (region.base <= module_last && last >= module.base) {
            return true;
        }
    }
    return false;
}

bool parse_u32(std::string_view text, std::uint32_t& out) noexcept {
    if (text.empty()) {
        return false;
    }
    std::uint64_t value = 0;
    for (char ch : text) {
        if (ch < '0' || ch > '9') {
            return false;
        }
        value = value * 10u + static_cast<std::uint64_t>(ch - '0');
        if (value > 0xFFFFFFFFull) {
            return false;
        }
    }
    out = static_cast<std::uint32_t>(value);
    return true;
}

bool parse_decimal(std::string_view text, double& out) noexcept {
    if (text.empty()) {
        return false;
    }
    std::size_t index = 0;
    if (text[index] == '+' || text[index] == '-') {
        ++index;
    }
    if (index >= text.size() || text[index] < '0' || text[index] > '9') {
        return false;
    }
    while (index < text.size() && text[index] >= '0' && text[index] <= '9') {
        ++index;
    }
    if (index < text.size() && text[index] == '.') {
        ++index;
        if (index >= text.size() || text[index] < '0' || text[index] > '9') {
            return false;
        }
        while (index < text.size() && text[index] >= '0' && text[index] <= '9') {
            ++index;
        }
    }
    if (index != text.size()) {
        return false;
    }
    std::string_view body = text;
    if (body.front() == '+') {
        body.remove_prefix(1);
    }
    double value = 0.0;
    const std::from_chars_result parsed = std::from_chars(
        body.data(), body.data() + body.size(), value, std::chars_format::fixed);
    if (parsed.ec != std::errc{} || parsed.ptr != body.data() + body.size() || !std::isfinite(value)) {
        return false;
    }
    out = value;
    return true;
}

bool extract_cleo_payload(std::string_view line, std::string_view& payload) noexcept {
    if (line.size() < 8 || line[2] != ':' || line[5] != ':') {
        return false;
    }
    for (const std::size_t index : {0u, 1u, 3u, 4u, 6u, 7u}) {
        if (line[index] < '0' || line[index] > '9') {
            return false;
        }
    }
    constexpr std::string_view kLevel = " [INFO] [\"";
    if (!line.substr(8).starts_with(kLevel)) {
        return false;
    }
    const std::size_t name_begin = 8 + kLevel.size();
    const std::size_t name_end = line.find("\"] \"", name_begin);
    if (name_end == std::string_view::npos || name_end == name_begin) {
        return false;
    }
    for (std::size_t index = name_begin; index < name_end; ++index) {
        const char ch = line[index];
        if (ch == '"' || ch == '[' || ch == ']' || ch < 0x20) {
            return false;
        }
    }
    if (line.back() != '"') {
        return false;
    }
    const std::size_t payload_begin = name_end + 4;
    if (payload_begin >= line.size()) {
        return false;
    }
    payload = line.substr(payload_begin, line.size() - payload_begin - 1);
    return payload.starts_with("DEFLUX14,");
}

bool known_phase(std::string_view phase) noexcept {
    for (const char* name : kPhases) {
        if (phase == name) {
            return true;
        }
    }
    return false;
}

bool accept_record(std::string_view line, std::uint64_t observed_ms, OracleSample& sample) noexcept {
    if (line.size() >= 1 && line.back() == '\r') {
        line.remove_suffix(1);
    }
    constexpr std::string_view kPrefix = "DEFLUX14,";
    std::string_view payload = line;
    if (!line.starts_with(kPrefix)) {
        if (!extract_cleo_payload(line, payload) || !payload.starts_with(kPrefix)) {
            return false;
        }
    }
    std::string_view rest = payload.substr(kPrefix.size());
    std::string_view fields[5]{};
    for (int field = 0; field < 5; ++field) {
        const std::size_t comma = rest.find(',');
        if (field < 4) {
            if (comma == std::string_view::npos) {
                return false;
            }
            fields[field] = rest.substr(0, comma);
            rest.remove_prefix(comma + 1);
        } else {
            if (comma != std::string_view::npos || rest.empty()) {
                return false;
            }
            fields[field] = rest;
        }
    }
    std::uint32_t sample_id = 0;
    double x = 0.0;
    double y = 0.0;
    double z = 0.0;
    if (!parse_u32(fields[0], sample_id) || !known_phase(fields[1]) || !parse_decimal(fields[2], x)
        || !parse_decimal(fields[3], y) || !parse_decimal(fields[4], z)) {
        return false;
    }
    if (grounded_oracle_invalid(GroundedVec3{x, y, z})) {
        return false;
    }
    sample.sample_id = sample_id;
    sample.phase = std::string(fields[1]);
    sample.observed_ms = observed_ms;
    sample.x = x;
    sample.y = y;
    sample.z = z;
    return true;
}

void clear_scan(StreamScanResult& result, const char* reason) {
    result.aborted = true;
    result.abort_reason = reason;
    result.hits.clear();
    result.float64_hits.clear();
}

bool decode_f32(const std::uint8_t bytes[4], double& out) noexcept {
    float value = 0.0f;
    std::memcpy(&value, bytes, sizeof(value));
    out = static_cast<double>(value);
    return std::isfinite(value) && !grounded_candidate_component_absurd(out);
}

bool decode_f64(const std::uint8_t bytes[8], double& out) noexcept {
    double value = 0.0;
    std::memcpy(&value, bytes, sizeof(value));
    out = value;
    return std::isfinite(value) && !grounded_candidate_component_absurd(value);
}

struct TrajectoryFlags {
    int stand_hits = 0;
    bool stale = false;
    bool double_failed = false;
    bool turn_ok = false;
    bool displacement_ok = false;
    bool matched[4] = {};
    GroundedVec3 memory[4]{};
};

GroundedVec3 standing_oracle(const GroundedObservation& observation, int index) noexcept {
    if (index == 0) {
        return observation.oracle_a;
    }
    if (index == 1) {
        return observation.oracle_c;
    }
    if (index == 2) {
        return observation.oracle_e;
    }
    return observation.oracle_h;
}

int standing_slot(int index) noexcept {
    if (index == 0) {
        return static_cast<int>(GroundedSlot::StandA1);
    }
    if (index == 1) {
        return static_cast<int>(GroundedSlot::StandC1);
    }
    if (index == 2) {
        return static_cast<int>(GroundedSlot::StandE1);
    }
    return static_cast<int>(GroundedSlot::StandH1);
}

int standing_second_slot(int index) noexcept {
    if (index == 0) {
        return static_cast<int>(GroundedSlot::StandA2);
    }
    if (index == 1) {
        return static_cast<int>(GroundedSlot::StandC2);
    }
    if (index == 2) {
        return static_cast<int>(GroundedSlot::StandE2);
    }
    return static_cast<int>(GroundedSlot::StandH2);
}

TrajectoryFlags analyze_trajectory(const GroundedObservation& observation) noexcept {
    TrajectoryFlags flags{};
    const GroundedVec3 offset{observation.offset_x, observation.offset_y, observation.offset_z};
    GroundedVec3 matched_oracles[4]{};
    int matched_count = 0;
    for (int index = 0; index < 4; ++index) {
        const SlotRead& first = observation.slots[standing_slot(index)];
        const SlotRead& second = observation.slots[standing_second_slot(index)];
        if (!first.present) {
            continue;
        }
        const GroundedVec3 memory{first.x, first.y, first.z};
        const GroundedVec3 oracle = standing_oracle(observation, index);
        if (matched_count > 0
            && grounded_is_stale(memory, oracle, matched_oracles, static_cast<std::size_t>(matched_count), offset)) {
            flags.stale = true;
            continue;
        }
        if (!observation.has_offset || !grounded_offset_holds(memory, oracle, offset)) {
            continue;
        }
        const GroundedVec3 second_memory{second.x, second.y, second.z};
        if (!second.present || !grounded_double_read_stable(memory, second_memory)
            || !grounded_offset_holds(second_memory, oracle, offset)) {
            flags.double_failed = true;
            continue;
        }
        flags.matched[index] = true;
        flags.memory[index] = memory;
        matched_oracles[matched_count] = oracle;
        ++matched_count;
        ++flags.stand_hits;
    }
    flags.displacement_ok = true;
    int previous = -1;
    for (int index = 0; index < 4; ++index) {
        if (!flags.matched[index]) {
            continue;
        }
        if (previous >= 0) {
            if (!grounded_displacement_agrees(standing_oracle(observation, previous),
                                              standing_oracle(observation, index),
                                              flags.memory[previous],
                                              flags.memory[index])) {
                flags.displacement_ok = false;
            }
        }
        previous = index;
    }
    if (flags.matched[2]) {
        const SlotRead& turn = observation.slots[static_cast<int>(GroundedSlot::TurnF)];
        if (turn.present) {
            flags.turn_ok = grounded_turn_stable(flags.memory[2], GroundedVec3{turn.x, turn.y, turn.z});
        }
    }
    return flags;
}

bool silence_lost(const GroundedObservation& observation, const TrajectoryFlags& flags) noexcept {
    if (!flags.matched[3]) {
        return false;
    }
    const SlotRead& silence = observation.slots[static_cast<int>(GroundedSlot::Silence)];
    if (!silence.present) {
        return true;
    }
    const GroundedVec3 held{silence.x, silence.y, silence.z};
    return !grounded_double_read_stable(flags.memory[3], held);
}

bool density_blocks_strong(const GroundedObservation& observation) noexcept {
    return observation.initial_page_matches > kGroundedDensePageInitial
        && observation.qualified_page_matches > kGroundedDensePageResolved;
}

SlotRead read_slot(const GroundedView& view, std::uint64_t address) noexcept {
    SlotRead read{};
    GroundedVec3 value{};
    if (!grounded_copy_triple(view, address, value)) {
        return read;
    }
    read.present = true;
    read.x = value.x;
    read.y = value.y;
    read.z = value.z;
    return read;
}

} // namespace

std::uint64_t grounded_observation_tick() noexcept {
    return GetTickCount64();
}

const char* grounded_phase_name(std::uint32_t index) noexcept {
    if (index >= grounded_phase_count()) {
        return "";
    }
    return kPhases[index];
}

std::uint32_t grounded_phase_count() noexcept {
    return static_cast<std::uint32_t>(sizeof(kPhases) / sizeof(kPhases[0]));
}

bool grounded_phase_is_standing(std::string_view phase) noexcept {
    return phase == "A_STAND" || phase == "C_STAND" || phase == "E_STAND" || phase == "H_STAND";
}

bool grounded_phase_duration_ok(std::uint64_t start_ms, std::uint64_t end_ms) noexcept {
    return end_ms >= start_ms && end_ms - start_ms >= kGroundedMinPhaseMs;
}

bool grounded_region_eligible(const GroundedRegion& region) noexcept {
    if (region.size < 12 || region.state != kCommit) {
        return false;
    }
    if ((region.protect & kGuard) != 0 || (region.protect & kNoAccess) != 0 || region.protect == 0) {
        return false;
    }
    if (region.type == kMapped || (region.type != kPrivate && region.type != kImage)) {
        return false;
    }
    if (executable_access(region.protect) || !readable_access(region.protect)) {
        return false;
    }
    return true;
}

bool grounded_module_excludes(GroundedModuleKind kind) noexcept {
    return kind != GroundedModuleKind::GameImage;
}

bool grounded_address_excluded(std::uint64_t address,
                               const GroundedModule* modules,
                               std::size_t module_count) noexcept {
    if (modules == nullptr) {
        return false;
    }
    for (std::size_t index = 0; index < module_count; ++index) {
        const GroundedModule& module = modules[index];
        if (!grounded_module_excludes(module.kind) || module.size == 0) {
            continue;
        }
        if (address >= module.base && address - module.base < module.size) {
            return true;
        }
    }
    return false;
}

#ifdef _MSC_VER
#pragma optimize("t", on)
#endif
bool grounded_buffer_contains_marker(const std::uint8_t* data, std::size_t size) noexcept {
    if (data == nullptr || size < sizeof(kMarkerAscii) - 1) {
        return false;
    }
    const std::size_t ascii_len = sizeof(kMarkerAscii) - 1;
    const std::size_t utf16_len = ascii_len * 2;
    for (std::size_t index = 0; index + ascii_len <= size;) {
        const void* found = std::memchr(data + index, static_cast<unsigned char>(kMarkerAscii[0]), size - index);
        if (found == nullptr) {
            return false;
        }
        index = static_cast<const std::uint8_t*>(found) - data;
        if (index + ascii_len <= size && std::memcmp(data + index, kMarkerAscii, ascii_len) == 0) {
            return true;
        }
        if (index + utf16_len <= size) {
            bool match = true;
            for (std::size_t character = 0; character < ascii_len; ++character) {
                if (data[index + character * 2] != static_cast<std::uint8_t>(kMarkerAscii[character])
                    || data[index + character * 2 + 1] != 0) {
                    match = false;
                    break;
                }
            }
            if (match) {
                return true;
            }
        }
        ++index;
    }
    return false;
}
#ifdef _MSC_VER
#pragma optimize("", on)
#endif

bool grounded_oracle_invalid(GroundedVec3 value) noexcept {
    return !std::isfinite(value.x) || !std::isfinite(value.y) || !std::isfinite(value.z)
        || std::fabs(value.x) > kGroundedOracleAbsurd || std::fabs(value.y) > kGroundedOracleAbsurd
        || std::fabs(value.z) > kGroundedOracleAbsurd;
}

bool grounded_candidate_component_absurd(double component) noexcept {
    return !std::isfinite(component) || std::fabs(component) > kGroundedCandidateAbsurd;
}

bool grounded_exact_match(GroundedVec3 memory, GroundedVec3 oracle) noexcept {
    return std::fabs(memory.x - oracle.x) <= kGroundedExactMeters
        && std::fabs(memory.y - oracle.y) <= kGroundedExactMeters
        && std::fabs(memory.z - oracle.z) <= kGroundedExactMeters;
}

bool grounded_near_match(GroundedVec3 memory, GroundedVec3 oracle) noexcept {
    const double dx = memory.x - oracle.x;
    const double dy = memory.y - oracle.y;
    const double dz = memory.z - oracle.z;
    const double limit = kGroundedNearMeters;
    return dx * dx + dy * dy + dz * dz <= limit * limit;
}

bool grounded_select_offset(GroundedVec3 memory, GroundedVec3 oracle, GroundedVec3& offset) noexcept {
    const GroundedVec3 catalog[3] = {
        {0.0, 0.0, 0.0},
        {0.0, 0.0, -kGroundedOffsetZ},
        {0.0, 0.0, kGroundedOffsetZ},
    };
    for (const GroundedVec3& candidate : catalog) {
        if (grounded_exact_match(memory, add_offset(oracle, candidate))
            || grounded_near_match(memory, add_offset(oracle, candidate))) {
            offset = candidate;
            return true;
        }
    }
    return false;
}

bool grounded_offset_holds(GroundedVec3 memory, GroundedVec3 oracle, GroundedVec3 offset) noexcept {
    const GroundedVec3 shifted = add_offset(oracle, offset);
    return grounded_exact_match(memory, shifted) || grounded_near_match(memory, shifted);
}

double grounded_distance(GroundedVec3 a, GroundedVec3 b) noexcept {
    const double dx = a.x - b.x;
    const double dy = a.y - b.y;
    const double dz = a.z - b.z;
    return std::sqrt(dx * dx + dy * dy + dz * dz);
}

bool grounded_is_stale(GroundedVec3 memory,
                       GroundedVec3 current_oracle,
                       const GroundedVec3* older_oracles,
                       std::size_t older_count,
                       GroundedVec3 offset) noexcept {
    if (older_oracles == nullptr || older_count == 0) {
        return false;
    }
    const double current = grounded_distance(memory, add_offset(current_oracle, offset));
    for (std::size_t index = 0; index < older_count; ++index) {
        const double older = grounded_distance(memory, add_offset(older_oracles[index], offset));
        if (older + kGroundedStaleGapMeters < current) {
            return true;
        }
    }
    return false;
}

bool grounded_displacement_agrees(GroundedVec3 oracle_from,
                                  GroundedVec3 oracle_to,
                                  GroundedVec3 memory_from,
                                  GroundedVec3 memory_to) noexcept {
    const double oracle_distance = grounded_distance(oracle_from, oracle_to);
    const double memory_distance = grounded_distance(memory_from, memory_to);
    const double limit = std::max(0.25, 0.02 * oracle_distance);
    if (std::fabs(memory_distance - oracle_distance) > limit) {
        return false;
    }
    const double ox = oracle_to.x - oracle_from.x;
    const double oy = oracle_to.y - oracle_from.y;
    const double cx = memory_to.x - memory_from.x;
    const double cy = memory_to.y - memory_from.y;
    const double oracle_horizontal = std::sqrt(ox * ox + oy * oy);
    const double memory_horizontal = std::sqrt(cx * cx + cy * cy);
    if (oracle_horizontal < 1e-9 || memory_horizontal < 1e-9) {
        return oracle_horizontal <= kGroundedTurnMeters && memory_horizontal <= kGroundedTurnMeters;
    }
    const double cosine = (ox * cx + oy * cy) / (oracle_horizontal * memory_horizontal);
    return cosine >= kGroundedDirectionCosine;
}

bool grounded_turn_stable(GroundedVec3 before, GroundedVec3 during) noexcept {
    return grounded_distance(before, during) <= kGroundedTurnMeters;
}

bool grounded_double_read_stable(GroundedVec3 first, GroundedVec3 second) noexcept {
    return grounded_distance(first, second) <= kGroundedNearMeters;
}

bool grounded_standing_positions_separated(GroundedVec3 a,
                                           GroundedVec3 b,
                                           GroundedVec3 c,
                                           GroundedVec3 d) noexcept {
    const GroundedVec3 points[4] = {a, b, c, d};
    for (int left = 0; left < 4; ++left) {
        for (int right = left + 1; right < 4; ++right) {
            if (grounded_distance(points[left], points[right]) < kGroundedMinSeparationMeters) {
                return false;
            }
        }
    }
    return true;
}

bool grounded_copy_triple(const GroundedView& view, std::uint64_t address, GroundedVec3& out) noexcept {
    std::uint8_t bytes[12]{};
    for (std::uint32_t index = 0; index < 12; ++index) {
        const std::uint64_t cursor = address + index;
        const GroundedRegion* region = region_at(view, cursor);
        if (region == nullptr || !grounded_region_eligible(*region) || region->bytes == nullptr) {
            return false;
        }
        if (grounded_address_excluded(cursor, view.modules, view.module_count)) {
            return false;
        }
        const std::uint64_t offset = cursor - region->base;
        if (offset >= region->size) {
            return false;
        }
        bytes[index] = region->bytes[offset];
    }
    double x = 0.0;
    double y = 0.0;
    double z = 0.0;
    if (!decode_f32(bytes, x) || !decode_f32(bytes + 4, y) || !decode_f32(bytes + 8, z)) {
        return false;
    }
    out = {x, y, z};
    return true;
}

bool grounded_standing_median(const OracleSample* samples, std::size_t count, GroundedVec3& median) noexcept {
    if (samples == nullptr || count == 0) {
        return false;
    }
    std::vector<double> xs;
    std::vector<double> ys;
    std::vector<double> zs;
    xs.reserve(count);
    ys.reserve(count);
    zs.reserve(count);
    for (std::size_t index = 0; index < count; ++index) {
        xs.push_back(samples[index].x);
        ys.push_back(samples[index].y);
        zs.push_back(samples[index].z);
    }
    const auto middle = [](std::vector<double>& values) {
        std::sort(values.begin(), values.end());
        const std::size_t mid = values.size() / 2;
        if ((values.size() % 2u) == 1u) {
            return values[mid];
        }
        return (values[mid - 1] + values[mid]) * 0.5;
    };
    median = {middle(xs), middle(ys), middle(zs)};
    return true;
}

bool grounded_standing_oracle_stable(const OracleSample* samples, std::size_t count) noexcept {
    GroundedVec3 median{};
    if (!grounded_standing_median(samples, count, median)) {
        return false;
    }
    for (std::size_t index = 0; index < count; ++index) {
        const GroundedVec3 sample{samples[index].x, samples[index].y, samples[index].z};
        if (grounded_oracle_invalid(sample) || !grounded_near_match(sample, median)) {
            return false;
        }
    }
    return true;
}

#ifdef _MSC_VER
#pragma optimize("t", on)
#endif
StreamScanResult stream_scan_float32(const StreamScanRequest& request) {
    StreamScanResult result{};
    result.timing.timeout_budget_ms = request.time_limit_ms;
    result.timing.timeout_stage = kGroundedStageNone;
    if (grounded_oracle_invalid(request.oracle)) {
        clear_scan(result, "oracle_invalid");
        return result;
    }
    const std::uint64_t started = clock_now(request.clock);
    std::uint64_t last_clock = started;
    auto note = [&](std::uint64_t now, std::uint64_t& bucket) {
        bucket += now - last_clock;
        last_clock = now;
    };
    auto fail_timeout = [&](const char* stage, std::uint64_t now) {
        result.timing.timeout_stage = stage;
        result.timing.timeout_budget_ms = request.time_limit_ms;
        result.timing.total_scan_ms = now - started;
        result.timing.eligible_bytes = result.bytes_considered;
        result.timing.partial_scan = result.timing.bytes_attempted > 0 || result.timing.bytes_read > 0;
        clear_scan(result, "timeout");
        return result;
    };

    std::vector<std::uint64_t> marker_bases;
    for (std::size_t index = 0; index < request.view.region_count; ++index) {
        const GroundedRegion& region = request.view.regions[index];
        if (region.type != kPrivate || !grounded_region_eligible(region) || region.bytes == nullptr) {
            continue;
        }
        if (region_fully_excluded(region, request.view.modules, request.view.module_count)) {
            continue;
        }
        if (grounded_buffer_contains_marker(region.bytes, static_cast<std::size_t>(region.size))) {
            marker_bases.push_back(region.base);
        }
    }

    for (std::size_t index = 0; index < request.view.region_count; ++index) {
        const std::uint64_t enumerated = clock_now(request.clock);
        note(enumerated, result.timing.enumeration_ms);
        if (enumerated - started > request.time_limit_ms) {
            return fail_timeout(kGroundedStageEnumeration, enumerated);
        }
        const GroundedRegion& region = request.view.regions[index];
        if (!grounded_region_eligible(region)) {
            continue;
        }
        if (region_fully_excluded(region, request.view.modules, request.view.module_count)) {
            continue;
        }
        if (region.size > request.byte_limit
            || result.bytes_considered > request.byte_limit - region.size) {
            clear_scan(result, "byte_budget");
            return result;
        }
        result.bytes_considered += region.size;
        ++result.timing.eligible_region_count;
        result.timing.eligible_bytes = result.bytes_considered;
        if (region.bytes == nullptr || region.size < 12) {
            continue;
        }
        result.timing.bytes_attempted += region.size;
        const std::uint64_t read_now = clock_now(request.clock);
        note(read_now, result.timing.read_ms);
        if (read_now - started > request.time_limit_ms) {
            return fail_timeout(kGroundedStageRead, read_now);
        }
        result.timing.bytes_read += region.size;
        const bool marked = std::find(marker_bases.begin(), marker_bases.end(), region.base) != marker_bases.end();
        const bool overlaps = region_overlaps_excluded(region, request.view.modules, request.view.module_count);
        const std::uint64_t region_end = region.base + region.size;
        std::uint64_t cursor = (region.base + 3ull) & ~3ull;
        std::uint64_t slice = 0;
        for (; cursor < region_end; cursor += 4) {
            GroundedVec3 value{};
            bool copied = false;
            if (!overlaps && cursor + 12 <= region_end) {
                const std::uint64_t local = cursor - region.base;
                copied = decode_f32(region.bytes + local, value.x)
                    && decode_f32(region.bytes + local + 4, value.y)
                    && decode_f32(region.bytes + local + 8, value.z);
            } else if (!grounded_address_excluded(cursor, request.view.modules, request.view.module_count)) {
                copied = grounded_copy_triple(request.view, cursor, value);
            }
            if (copied && grounded_triple_can_match(value, request.oracle)) {
                GroundedVec3 offset{};
                if (grounded_select_offset(value, request.oracle, offset)) {
                    if (result.hits.size() >= request.candidate_limit) {
                        clear_scan(result, "candidate_cap");
                        return result;
                    }
                    GroundedHit hit{};
                    hit.address = cursor;
                    hit.x = value.x;
                    hit.y = value.y;
                    hit.z = value.z;
                    hit.offset_x = offset.x;
                    hit.offset_y = offset.y;
                    hit.offset_z = offset.z;
                    hit.marker_allocation = marked;
                    result.hits.push_back(hit);
                }
            }
            slice += 4;
            if (slice >= kGroundedScanTimeSliceBytes) {
                slice = 0;
                const std::uint64_t matched = clock_now(request.clock);
                note(matched, result.timing.match_ms);
                if (matched - started > request.time_limit_ms) {
                    return fail_timeout(kGroundedStageMatch, matched);
                }
            }
        }
        if (region.size >= 24) {
            for (std::uint64_t wide = region.base; wide + 24 <= region_end; wide += 8) {
                Float64Diagnostic diagnostic{};
                bool copied = false;
                if (!overlaps) {
                    const std::uint64_t local = wide - region.base;
                    copied = decode_f64(region.bytes + local, diagnostic.x)
                        && decode_f64(region.bytes + local + 8, diagnostic.y)
                        && decode_f64(region.bytes + local + 16, diagnostic.z);
                } else if (!grounded_address_excluded(wide, request.view.modules, request.view.module_count)) {
                    std::uint8_t raw[24]{};
                    copied = true;
                    for (std::uint32_t byte = 0; byte < 24; ++byte) {
                        const GroundedRegion* host = region_at(request.view, wide + byte);
                        if (host == nullptr || host->bytes == nullptr || !grounded_region_eligible(*host)) {
                            copied = false;
                            break;
                        }
                        raw[byte] = host->bytes[(wide + byte) - host->base];
                    }
                    if (copied) {
                        copied = decode_f64(raw, diagnostic.x) && decode_f64(raw + 8, diagnostic.y)
                            && decode_f64(raw + 16, diagnostic.z);
                    }
                }
                if (!copied) {
                    continue;
                }
                const GroundedVec3 wide_value{diagnostic.x, diagnostic.y, diagnostic.z};
                GroundedVec3 offset{};
                if (!grounded_triple_can_match(wide_value, request.oracle)
                    || !grounded_select_offset(wide_value, request.oracle, offset)) {
                    continue;
                }
                diagnostic.address = wide;
                result.float64_hits.push_back(diagnostic);
            }
        }
    }

    const std::uint64_t filter_now = clock_now(request.clock);
    note(filter_now, result.timing.match_ms);
    if (filter_now - started > request.time_limit_ms) {
        return fail_timeout(kGroundedStageFilter, filter_now);
    }
    std::vector<std::uint32_t> counts(result.hits.size(), 0);
    for (std::size_t left = 0; left < result.hits.size(); ++left) {
        const std::uint64_t page = result.hits[left].address / kGroundedPageBytes;
        std::uint32_t count = 0;
        for (const GroundedHit& hit : result.hits) {
            if (hit.address / kGroundedPageBytes == page) {
                ++count;
            }
        }
        counts[left] = count;
    }
    for (std::size_t index = 0; index < result.hits.size(); ++index) {
        result.hits[index].initial_page_matches = counts[index];
    }
    std::sort(result.hits.begin(), result.hits.end(), [](const GroundedHit& left, const GroundedHit& right) {
        return left.address < right.address;
    });
    std::sort(result.float64_hits.begin(),
              result.float64_hits.end(),
              [](const Float64Diagnostic& left, const Float64Diagnostic& right) {
                  return left.address < right.address;
              });
    const std::uint64_t finished = clock_now(request.clock);
    note(finished, result.timing.filter_ms);
    result.timing.total_scan_ms = finished - started;
    result.timing.timeout_stage = kGroundedStageNone;
    result.timing.partial_scan = false;
    result.timing.eligible_bytes = result.bytes_considered;
    return result;
}
#ifdef _MSC_VER
#pragma optimize("", on)
#endif

RecheckResult recheck_grounded_hits(const RecheckRequest& request) {
    RecheckResult result{};
    const std::uint64_t started = clock_now(request.clock);
    if (request.hits == nullptr || request.hit_count == 0) {
        return result;
    }
    for (std::size_t index = 0; index < request.hit_count; ++index) {
        const std::uint64_t now = clock_now(request.clock);
        if (now - started > request.time_limit_ms) {
            result.aborted = true;
            result.abort_reason = "recheck_timeout";
            result.hits.clear();
            return result;
        }
        const GroundedHit& original = request.hits[index];
        GroundedVec3 value{};
        if (!grounded_copy_triple(request.view, original.address, value)) {
            continue;
        }
        const GroundedVec3 offset{original.offset_x, original.offset_y, original.offset_z};
        if (!grounded_offset_holds(value, request.oracle, offset)) {
            continue;
        }
        GroundedHit kept = original;
        kept.x = value.x;
        kept.y = value.y;
        kept.z = value.z;
        result.hits.push_back(kept);
    }
    return result;
}

GroundedClassification classify_grounded_observation(const GroundedObservation& observation) noexcept {
    GroundedClassification classification{};
    if (observation.float64_only || observation.marker_allocation) {
        classification.evidence = GroundedEvidence::CleoContamination;
        classification.reason = observation.float64_only ? "float64_only" : "marker_allocation";
        return classification;
    }
    if (!observation.has_offset) {
        classification.evidence = GroundedEvidence::Rejected;
        classification.reason = "no_catalog_offset";
        return classification;
    }
    const TrajectoryFlags flags = analyze_trajectory(observation);
    if (silence_lost(observation, flags)) {
        classification.evidence = GroundedEvidence::CleoContamination;
        classification.reason = "silence_disappeared";
        return classification;
    }
    if (flags.stale) {
        classification.evidence = GroundedEvidence::StaleCopy;
        classification.reason = "stale_copy";
        return classification;
    }
    const bool body_ok = flags.turn_ok && flags.displacement_ok && !flags.double_failed;
    if (flags.stand_hits == 4 && body_ok && !density_blocks_strong(observation)) {
        classification.evidence = GroundedEvidence::Strong;
        classification.reason = "four_stands";
        return classification;
    }
    if (flags.stand_hits == 4 && body_ok && density_blocks_strong(observation)) {
        classification.evidence = GroundedEvidence::Moderate;
        classification.reason = "dense_page_unresolved";
        return classification;
    }
    if (flags.stand_hits >= 3 && flags.turn_ok && !flags.double_failed) {
        classification.evidence = GroundedEvidence::Moderate;
        classification.reason = "three_stands";
        return classification;
    }
    if (flags.stand_hits == 2 && !flags.double_failed) {
        classification.evidence = GroundedEvidence::Weak;
        classification.reason = "two_stands";
        return classification;
    }
    classification.evidence = GroundedEvidence::Rejected;
    classification.reason = flags.double_failed ? "double_read" : "trajectory";
    return classification;
}

GroundedQualification qualify_grounded_session(GroundedQualifyRequest& request) {
    GroundedQualification qualification{};
    if (request.in_vehicle || grounded_vehicle_aborts_session(request.in_vehicle)) {
        qualification.aborted = true;
        qualification.abort_reason = "vehicle_semantic_change";
        return qualification;
    }
    if (grounded_oracle_invalid(request.oracle_a) || grounded_oracle_invalid(request.oracle_c)
        || grounded_oracle_invalid(request.oracle_e) || grounded_oracle_invalid(request.oracle_h)) {
        qualification.aborted = true;
        qualification.abort_reason = "oracle_invalid";
        return qualification;
    }
    if (!grounded_standing_positions_separated(request.oracle_a, request.oracle_c, request.oracle_e, request.oracle_h)) {
        qualification.aborted = true;
        qualification.abort_reason = "standing_positions_too_close";
        return qualification;
    }
    if (request.prepare == nullptr) {
        qualification.aborted = true;
        qualification.abort_reason = "prepare_missing";
        return qualification;
    }

    request.prepare(request.context, static_cast<std::uint32_t>(GroundedSlot::StandA1));
    StreamScanRequest scan{};
    scan.view = request.view;
    scan.oracle = request.oracle_a;
    scan.clock = request.clock;
    StreamScanResult scanned = stream_scan_float32(scan);
    if (scanned.aborted) {
        qualification.aborted = true;
        qualification.abort_reason = scanned.abort_reason;
        return qualification;
    }

    std::vector<GroundedObservation> observations;
    observations.reserve(scanned.hits.size() + scanned.float64_hits.size());
    for (const GroundedHit& hit : scanned.hits) {
        GroundedObservation observation{};
        observation.address = hit.address;
        observation.offset_x = hit.offset_x;
        observation.offset_y = hit.offset_y;
        observation.offset_z = hit.offset_z;
        observation.has_offset = true;
        observation.marker_allocation = hit.marker_allocation;
        observation.initial_page_matches = hit.initial_page_matches;
        observation.oracle_a = request.oracle_a;
        observation.oracle_c = request.oracle_c;
        observation.oracle_e = request.oracle_e;
        observation.oracle_h = request.oracle_h;
        observation.slots[static_cast<int>(GroundedSlot::StandA1)] = {true, hit.x, hit.y, hit.z};
        const GroundedSlot later[] = {
            GroundedSlot::StandA2,
            GroundedSlot::StandC1,
            GroundedSlot::StandC2,
            GroundedSlot::StandE1,
            GroundedSlot::StandE2,
            GroundedSlot::TurnF,
            GroundedSlot::StandH1,
            GroundedSlot::StandH2,
            GroundedSlot::Silence,
        };
        for (GroundedSlot slot : later) {
            request.prepare(request.context, static_cast<std::uint32_t>(slot));
            if (slot == GroundedSlot::StandC1 || slot == GroundedSlot::StandE1 || slot == GroundedSlot::StandH1
                || slot == GroundedSlot::TurnF) {
                RecheckRequest recheck{};
                recheck.hits = &hit;
                recheck.hit_count = 1;
                recheck.view = request.view;
                recheck.oracle = slot == GroundedSlot::StandC1   ? request.oracle_c
                                 : slot == GroundedSlot::StandE1 ? request.oracle_e
                                 : slot == GroundedSlot::StandH1 ? request.oracle_h
                                                                 : request.oracle_e;
                recheck.clock = request.clock;
                const RecheckResult checked = recheck_grounded_hits(recheck);
                if (checked.aborted) {
                    qualification.aborted = true;
                    qualification.abort_reason = checked.abort_reason;
                    qualification.addresses.clear();
                    return qualification;
                }
            }
            observation.slots[static_cast<int>(slot)] = read_slot(request.view, hit.address);
        }
        observations.push_back(observation);
    }
    for (const Float64Diagnostic& diagnostic : scanned.float64_hits) {
        bool also_float32 = false;
        for (const GroundedHit& hit : scanned.hits) {
            if (hit.address == diagnostic.address) {
                also_float32 = true;
                break;
            }
        }
        if (also_float32) {
            continue;
        }
        GroundedObservation observation{};
        observation.address = diagnostic.address;
        observation.float64_only = true;
        observation.oracle_a = request.oracle_a;
        observations.push_back(observation);
    }

    std::vector<std::uint32_t> qualified(observations.size(), 0);
    for (std::size_t index = 0; index < observations.size(); ++index) {
        const TrajectoryFlags flags = analyze_trajectory(observations[index]);
        const bool qualifies = flags.stand_hits == 4 && flags.turn_ok && flags.displacement_ok && !flags.double_failed
            && !flags.stale && !observations[index].marker_allocation && !observations[index].float64_only
            && !silence_lost(observations[index], flags);
        if (!qualifies) {
            continue;
        }
        const std::uint64_t page = observations[index].address / kGroundedPageBytes;
        for (std::size_t other = 0; other < observations.size(); ++other) {
            if (observations[other].address / kGroundedPageBytes == page) {
                const TrajectoryFlags other_flags = analyze_trajectory(observations[other]);
                const bool other_ok = other_flags.stand_hits == 4 && other_flags.turn_ok && other_flags.displacement_ok
                    && !other_flags.double_failed && !other_flags.stale && !observations[other].marker_allocation
                    && !silence_lost(observations[other], other_flags);
                if (other_ok) {
                    ++qualified[index];
                }
            }
        }
    }
    for (std::size_t index = 0; index < observations.size(); ++index) {
        observations[index].qualified_page_matches = std::max<std::uint32_t>(qualified[index], 1);
        if (observations[index].initial_page_matches <= kGroundedDensePageInitial) {
            observations[index].qualified_page_matches = 1;
        }
        const GroundedClassification classification = classify_grounded_observation(observations[index]);
        ClassifiedAddress address{};
        address.address = observations[index].address;
        address.evidence = classification.evidence;
        address.reason = classification.reason;
        address.float64_only = observations[index].float64_only;
        qualification.addresses.push_back(address);
    }
    std::sort(qualification.addresses.begin(),
              qualification.addresses.end(),
              [](const ClassifiedAddress& left, const ClassifiedAddress& right) {
                  return left.address < right.address;
              });
    return qualification;
}

bool grounded_live_requirements_met(std::size_t strong_count, bool cleo_absent_control_passed) noexcept {
    return strong_count >= 1 && cleo_absent_control_passed;
}

const char* grounded_session_claim() noexcept {
    return kGroundedLiveClaimWithheld;
}

LiveAttachRefusal evaluate_grounded_live_attach() noexcept {
    LiveAttachRefusal refusal{};
    refusal.blocked = true;
    char value[8]{};
    const DWORD length = GetEnvironmentVariableA(
        "DEFLUX_RESEARCH_ISOLATE_LIVE_TARGET", value, static_cast<DWORD>(sizeof(value)));
    if (length == 1 && value[0] == '1') {
        refusal.message = "Live GTA resolution is isolated from automated tests.";
        return refusal;
    }
    refusal.message = "Live attach is not implemented for the grounded scanner.";
    return refusal;
}

bool grounded_process_identity_holds(std::uint32_t expected_pid,
                                     std::uint64_t expected_creation,
                                     std::string_view expected_path,
                                     std::uint32_t live_pid,
                                     std::uint64_t live_creation,
                                     std::string_view live_path) noexcept {
    deflux::core::ProcessInstance expected{};
    expected.process_id = expected_pid;
    expected.creation_identity = expected_creation;
    expected.executable_path = std::string(expected_path);
    deflux::core::ProcessInstance live{};
    live.process_id = live_pid;
    live.creation_identity = live_creation;
    live.executable_path = std::string(live_path);
    return deflux::core::is_valid_process_instance(expected) && deflux::core::is_valid_process_instance(live)
        && deflux::core::same_process_instance(expected, live);
}

bool grounded_vehicle_aborts_session(bool in_vehicle) noexcept {
    return in_vehicle;
}

const char* to_string(GroundedEvidence evidence) noexcept {
    switch (evidence) {
    case GroundedEvidence::Strong:
        return "GROUND_TRUTH_STRONG";
    case GroundedEvidence::Moderate:
        return "GROUND_TRUTH_MODERATE";
    case GroundedEvidence::Weak:
        return "GROUND_TRUTH_WEAK";
    case GroundedEvidence::CleoContamination:
        return "CLEO_CONTAMINATION_SUSPECT";
    case GroundedEvidence::StaleCopy:
        return "STALE_COPY";
    case GroundedEvidence::Rejected:
        return "REJECTED";
    case GroundedEvidence::Unclassified:
        break;
    }
    return "UNCLASSIFIED";
}

void OracleLogWatcher::start_after_existing(std::uint64_t existing_size) {
    cursor_ = existing_size;
    started_ = true;
    pending_.clear();
    samples_.clear();
    accepted_ = 0;
    rejected_ = 0;
}

void OracleLogWatcher::consume_pending(std::uint64_t observed_ms) {
    std::size_t line_end = pending_.find('\n');
    while (line_end != std::string::npos) {
        const std::string_view line(pending_.data(), line_end);
        OracleSample sample{};
        if (accept_record(line, observed_ms, sample)) {
            samples_.push_back(std::move(sample));
            ++accepted_;
        } else if (!line.empty() && line != "\r") {
            ++rejected_;
        }
        pending_.erase(0, line_end + 1);
        line_end = pending_.find('\n');
    }
}

void OracleLogWatcher::poll(std::string_view contents, std::uint64_t observed_ms) {
    if (!started_) {
        start_after_existing(0);
    }
    if (contents.size() < cursor_) {
        samples_.clear();
        pending_.clear();
        cursor_ = 0;
        ++generation_;
        accepted_ = 0;
        rejected_ = 0;
    }
    if (contents.size() > cursor_) {
        pending_.append(contents.substr(static_cast<std::size_t>(cursor_)));
        cursor_ = contents.size();
    }
    consume_pending(observed_ms);
}

bool OracleLogWatcher::poll_file(const std::wstring& path, std::uint64_t observed_ms) {
    if (!started_) {
        start_after_existing(0);
    }
    const HANDLE handle = CreateFileW(path.c_str(),
                                       GENERIC_READ,
                                       FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                                       nullptr,
                                       OPEN_EXISTING,
                                       FILE_ATTRIBUTE_NORMAL,
                                       nullptr);
    if (handle == INVALID_HANDLE_VALUE) {
        return false;
    }
    LARGE_INTEGER size{};
    if (!GetFileSizeEx(handle, &size) || size.QuadPart < 0) {
        CloseHandle(handle);
        return false;
    }
    const auto file_size = static_cast<std::uint64_t>(size.QuadPart);
    if (file_size < cursor_) {
        samples_.clear();
        pending_.clear();
        cursor_ = 0;
        ++generation_;
        accepted_ = 0;
        rejected_ = 0;
    }
    if (file_size > cursor_) {
        const std::uint64_t count = file_size - cursor_;
        std::string bytes(static_cast<std::size_t>(count), '\0');
        LARGE_INTEGER offset{};
        offset.QuadPart = static_cast<LONGLONG>(cursor_);
        if (!SetFilePointerEx(handle, offset, nullptr, FILE_BEGIN)) {
            CloseHandle(handle);
            return false;
        }
        std::uint64_t got = 0;
        while (got < count) {
            DWORD chunk = 0;
            const auto request = static_cast<DWORD>(std::min<std::uint64_t>(count - got, 1ull << 20));
            if (!ReadFile(handle, bytes.data() + static_cast<std::size_t>(got), request, &chunk, nullptr)
                || chunk == 0) {
                CloseHandle(handle);
                return false;
            }
            got += chunk;
        }
        pending_.append(bytes);
        cursor_ = file_size;
    }
    CloseHandle(handle);
    consume_pending(observed_ms);
    return true;
}

void OracleLogWatcher::poll_observed(std::string_view contents) {
    poll(contents, grounded_observation_tick());
}

std::vector<OracleSample> OracleLogWatcher::samples_in_window(std::uint64_t center_ms,
                                                             std::uint32_t half_window_ms) const {
    std::vector<OracleSample> selected;
    for (const OracleSample& sample : samples_) {
        const std::uint64_t delta = sample.observed_ms > center_ms ? sample.observed_ms - center_ms
                                                                   : center_ms - sample.observed_ms;
        if (delta <= half_window_ms) {
            selected.push_back(sample);
        }
    }
    return selected;
}

} // namespace deflux::game::gta_sa_de
