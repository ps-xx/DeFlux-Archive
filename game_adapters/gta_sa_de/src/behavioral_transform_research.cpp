#include "deflux/game/gta_sa_de/behavioral_transform_research.hpp"

#include "deflux/game/gta_sa_de/research_evidence_log.hpp"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>

#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>

namespace deflux::game::gta_sa_de {
namespace {

constexpr std::uint32_t kPhaseMagic = 0x31504150u;
constexpr std::uint32_t kAckMagic = 0x314B4341u;
constexpr const char* kPrimaryPhases[] = {
    "BASELINE_STANDING",
    "MOVE_FORWARD",
    "STANDING_AFTER_FORWARD",
    "MOVE_BACKWARD",
    "STANDING_AFTER_BACKWARD",
    "TURN_LEFT",
    "STANDING_AFTER_TURN",
    "RUN_FORWARD",
    "FINAL_STANDING",
};
constexpr const char* kValidationPhases[] = {
    "BASELINE_STANDING",
    "MOVE_FORWARD",
    "STANDING_AFTER_FORWARD",
    "TURN_LEFT",
    "FINAL_STANDING",
};
constexpr const char* kCameraPhases[] = {
    "PLAYER_STILL_CAMERA_ROTATE",
    "PLAYER_STILL_AFTER_CAMERA_ROTATE",
};

const char* const* phases_for(BehavioralProtocol protocol, std::uint32_t& count) noexcept {
    if (protocol == BehavioralProtocol::Validation) {
        count = static_cast<std::uint32_t>(sizeof(kValidationPhases) / sizeof(kValidationPhases[0]));
        return kValidationPhases;
    }
    count = static_cast<std::uint32_t>(sizeof(kPrimaryPhases) / sizeof(kPrimaryPhases[0]));
    return kPrimaryPhases;
}

bool same_phase(const std::string& phase, const char* name) noexcept {
    return name != nullptr && phase == name;
}

const PhaseObservation* latest(const TransformCandidate& candidate, const char* phase) noexcept {
    const PhaseObservation* found = nullptr;
    for (const PhaseObservation& observation : candidate.observations) {
        if (same_phase(observation.phase, phase) && observation.present) {
            found = &observation;
        }
    }
    return found;
}

bool burst_stable(const TransformCandidate& candidate, const char* phase) noexcept {
    const PhaseObservation* first = nullptr;
    for (const PhaseObservation& observation : candidate.observations) {
        if (!same_phase(observation.phase, phase) || !observation.present) {
            continue;
        }
        if (!plausible_position(observation.value)) {
            return false;
        }
        if (first != nullptr
            && position_distance(first->value, observation.value) > kBehavioralStandingTolerance) {
            return false;
        }
        if (first == nullptr) {
            first = &observation;
        }
    }
    return first != nullptr;
}

float dot_delta(Vec3 from, Vec3 to, Vec3 other_from, Vec3 other_to) noexcept {
    const float ax = to.x - from.x;
    const float ay = to.y - from.y;
    const float az = to.z - from.z;
    const float bx = other_to.x - other_from.x;
    const float by = other_to.y - other_from.y;
    const float bz = other_to.z - other_from.z;
    return ax * bx + ay * by + az * bz;
}

bool readable_protect(std::uint32_t protect) noexcept {
    const std::uint32_t access = protect & 0xFFu;
    return access == kResearchPageReadOnly || access == kResearchPageReadWrite
        || access == kResearchPageWriteCopy;
}

bool executable_protect(std::uint32_t protect) noexcept {
    const std::uint32_t access = protect & 0xFFu;
    return (access & kResearchPageExecute) != 0;
}

#pragma pack(push, 1)
struct PhaseIpcRecord {
    std::uint32_t magic = 0;
    std::uint32_t sequence = 0;
    std::uint64_t session_nonce = 0;
    std::uint64_t timestamp_ms = 0;
    std::uint32_t protocol = 0;
    std::uint32_t stage = 0;
    std::uint32_t coverage_window = 0;
    char phase[48]{};
};

struct AckIpcRecord {
    std::uint32_t magic = 0;
    std::uint32_t sequence = 0;
    std::uint64_t session_nonce = 0;
    std::uint32_t ok = 0;
    std::uint32_t stage = 0;
    char message[160]{};
};
#pragma pack(pop)

void mapping_name(const wchar_t* prefix, std::uint64_t nonce, wchar_t* out, std::size_t count) {
    swprintf_s(out,
               count,
               L"%s%016llX",
               prefix,
               static_cast<unsigned long long>(nonce));
}

std::uint64_t tick_ms() noexcept {
    return GetTickCount64();
}

struct StoredPage {
    std::uint64_t base = 0;
    std::uint64_t region_base = 0;
    std::vector<std::uint8_t> bytes;
};

bool range_readable(const void* address, std::size_t size) noexcept {
    if (address == nullptr || size == 0) {
        return false;
    }
    const auto* cursor = static_cast<const std::uint8_t*>(address);
    const auto* end = cursor + size;
    while (cursor < end) {
        MEMORY_BASIC_INFORMATION info{};
        if (VirtualQuery(cursor, &info, sizeof(info)) == 0) {
            return false;
        }
        if (info.State != MEM_COMMIT || (info.Protect & PAGE_GUARD) != 0
            || (info.Protect & PAGE_NOACCESS) != 0) {
            return false;
        }
        const std::uint32_t access = info.Protect & 0xFFu;
        const bool readable = access == PAGE_READONLY || access == PAGE_READWRITE
            || access == PAGE_WRITECOPY || access == PAGE_EXECUTE_READ
            || access == PAGE_EXECUTE_READWRITE || access == PAGE_EXECUTE_WRITECOPY;
        if (!readable) {
            return false;
        }
        const auto* region_end = static_cast<const std::uint8_t*>(info.BaseAddress) + info.RegionSize;
        if (region_end <= cursor) {
            return false;
        }
        cursor = region_end;
    }
    return true;
}

bool read_bytes(std::uint64_t address, void* destination, std::size_t size) noexcept {
    if (destination == nullptr || size == 0 || address == 0) {
        return false;
    }
    const void* pointer = reinterpret_cast<const void*>(static_cast<std::uintptr_t>(address));
    if (!range_readable(pointer, size)) {
        return false;
    }
    std::memcpy(destination, pointer, size);
    return true;
}

void log_budget(const char* stage, const ScanBudget& budget) noexcept {
    char detail[640]{};
    std::snprintf(detail,
                  sizeof(detail),
                  "stage=%s regions_considered=%u regions_accepted=%u bytes_considered=%llu "
                  "bytes_sampled=%llu candidates_before=%u candidates_after=%u dense_pages=%u elapsed_ms=%u "
                  "partial=%d coverage_version=%u window_index=%u window_count=%u eligible_before=%llu "
                  "window_start=0x%llx window_end=0x%llx regions_sampled=%u total_eligible=%llu abort=%s",
                  stage,
                  budget.regions_considered,
                  budget.regions_accepted,
                  static_cast<unsigned long long>(budget.bytes_considered),
                  static_cast<unsigned long long>(budget.bytes_sampled),
                  budget.candidates_before_filter,
                  budget.candidates_after_filter,
                  budget.dense_pages_rejected,
                  budget.elapsed_ms,
                  budget.coverage_partial ? 1 : 0,
                  budget.coverage_strategy_version,
                  budget.coverage_window_index,
                  budget.coverage_window_count,
                  static_cast<unsigned long long>(budget.eligible_bytes_before_window),
                  static_cast<unsigned long long>(budget.coverage_window_start),
                  static_cast<unsigned long long>(budget.coverage_window_end),
                  budget.regions_sampled,
                  static_cast<unsigned long long>(budget.total_eligible_bytes),
                  budget.abort_reason != nullptr ? budget.abort_reason : "");
    research_evidence_append_lifecycle("BEHAVIORAL_BUDGET", detail);
}

void log_candidate(const TransformCandidate& candidate, const PhaseObservation& observation) noexcept {
    char detail[384]{};
    std::snprintf(detail,
                  sizeof(detail),
                  "phase=%s seq=%u nonce=%llu t_ms=%llu addr=0x%llx region=0x%llx "
                  "x=%.4f y=%.4f z=%.4f present=%d class=%s",
                  observation.phase.c_str(),
                  observation.sequence,
                  static_cast<unsigned long long>(observation.session_nonce),
                  static_cast<unsigned long long>(observation.timestamp_ms),
                  static_cast<unsigned long long>(candidate.address),
                  static_cast<unsigned long long>(candidate.region_base),
                  static_cast<double>(observation.value.x),
                  static_cast<double>(observation.value.y),
                  static_cast<double>(observation.value.z),
                  observation.present ? 1 : 0,
                  to_string(candidate.classification));
    research_evidence_append_lifecycle("BEHAVIORAL_CANDIDATE", detail);
}

HANDLE& held_ack_mapping() noexcept {
    static HANDLE mapping = nullptr;
    return mapping;
}

bool publish_ack(std::uint64_t nonce,
                 std::uint32_t sequence,
                 PhaseStage stage,
                 bool ok,
                 const char* message) noexcept {
    wchar_t name[96]{};
    mapping_name(L"Local\\DeFlux_11A_PhaseAck_", nonce, name, 96);
    if (held_ack_mapping() == nullptr) {
        held_ack_mapping() = CreateFileMappingW(INVALID_HANDLE_VALUE,
                                                nullptr,
                                                PAGE_READWRITE,
                                                0,
                                                sizeof(AckIpcRecord),
                                                name);
    }
    HANDLE mapping = held_ack_mapping();
    if (mapping == nullptr) {
        return false;
    }
    auto* view = static_cast<AckIpcRecord*>(
        MapViewOfFile(mapping, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(AckIpcRecord)));
    if (view == nullptr) {
        return false;
    }
    AckIpcRecord record{};
    record.magic = kAckMagic;
    record.sequence = sequence;
    record.session_nonce = nonce;
    record.ok = ok ? 1u : 0u;
    record.stage = static_cast<std::uint32_t>(stage);
    if (message != nullptr) {
        strncpy_s(record.message, message, _TRUNCATE);
    }
    *view = record;
    UnmapViewOfFile(view);
    return true;
}

bool wait_marker(std::uint64_t nonce,
                 std::uint32_t expected_sequence,
                 PhaseStage expected_stage,
                 BehavioralProtocol protocol,
                 PhaseMarker& out,
                 std::string& error) {
    wchar_t name[96]{};
    mapping_name(L"Local\\DeFlux_11A_Phase_", nonce, name, 96);
    const std::uint64_t deadline = tick_ms() + kBehavioralMarkerWaitMs;
    while (tick_ms() < deadline) {
        HANDLE mapping = OpenFileMappingW(FILE_MAP_READ, FALSE, name);
        if (mapping == nullptr) {
            Sleep(50);
            continue;
        }
        const auto* view = static_cast<const PhaseIpcRecord*>(
            MapViewOfFile(mapping, FILE_MAP_READ, 0, 0, sizeof(PhaseIpcRecord)));
        if (view == nullptr) {
            CloseHandle(mapping);
            Sleep(50);
            continue;
        }
        PhaseIpcRecord record = *view;
        UnmapViewOfFile(view);
        CloseHandle(mapping);
        if (record.magic != kPhaseMagic || record.session_nonce != nonce
            || record.sequence != expected_sequence
            || record.stage != static_cast<std::uint32_t>(expected_stage)) {
            Sleep(50);
            continue;
        }
        out.session_nonce = record.session_nonce;
        out.sequence = record.sequence;
        out.timestamp_ms = record.timestamp_ms;
        out.protocol = static_cast<BehavioralProtocol>(record.protocol);
        out.stage = static_cast<PhaseStage>(record.stage);
        out.coverage_window = record.coverage_window;
        out.phase = record.phase;
        if (expected_sequence == 1
            && behavioral_phase_in_order(out.protocol, 1, out.phase.c_str())) {
            return true;
        }
        if (out.protocol != protocol
            || !behavioral_phase_in_order(protocol, expected_sequence, out.phase.c_str())) {
            const bool camera = protocol == BehavioralProtocol::Primary
                && (out.phase == kCameraPhases[0] || out.phase == kCameraPhases[1]);
            const std::uint32_t count = behavioral_phase_count(protocol);
            const bool camera_slot = camera
                && (expected_sequence == count + 1 || expected_sequence == count + 2);
            if (!camera_slot) {
                error = "Phase marker is out of order.";
                return false;
            }
        }
        return true;
    }
    error = "Timed out waiting for a phase marker.";
    return false;
}

void apply_coverage_selection(ScanBudget& budget, const CoverageSelection& selection) noexcept {
    budget.coverage_strategy_version = selection.strategy_version;
    budget.coverage_window_index = selection.window_index;
    budget.coverage_window_count = selection.window_count;
    budget.regions_sampled = selection.regions_selected;
    budget.total_eligible_bytes = selection.total_eligible_bytes;
    budget.eligible_bytes_before_window = selection.eligible_bytes_before_window;
    budget.coverage_window_start = selection.window_start;
    budget.coverage_window_end = selection.window_end;
    budget.coverage_partial = selection.partial;
}

bool capture_baseline(ScanBudget& budget,
                      std::vector<StoredPage>& pages,
                      std::uint32_t coverage_window,
                      std::string& error) {
    const std::uint64_t started = tick_ms();
    std::vector<EligibleSpan> spans;
    std::uint64_t address = 0;
    while (address < 0x00007FFFFFFEFFFFull) {
        if (tick_ms() - started > kBehavioralMaxCaptureMs) {
            budget.aborted = true;
            budget.abort_reason = "capture_time";
            error = "Baseline enumeration exceeded the capture time limit.";
            return false;
        }
        MEMORY_BASIC_INFORMATION info{};
        if (VirtualQuery(reinterpret_cast<void*>(static_cast<std::uintptr_t>(address)),
                         &info,
                         sizeof(info))
            == 0) {
            break;
        }
        RegionDescriptor region{};
        region.base = reinterpret_cast<std::uint64_t>(info.BaseAddress);
        region.size = static_cast<std::uint64_t>(info.RegionSize);
        region.state = info.State;
        region.protect = info.Protect;
        region.type = info.Type;
        if (!note_research_region(budget, region)) {
            error = budget.abort_reason != nullptr ? budget.abort_reason : "region limit";
            return false;
        }
        if (classify_research_region(region).accept) {
            spans.push_back(EligibleSpan{region.base, region.size});
        }
        if (region.size == 0) {
            break;
        }
        const std::uint64_t next = region.base + region.size;
        if (next <= address) {
            break;
        }
        address = next;
    }
    CoverageSelection selection{};
    std::vector<CoverageSlice> slices;
    if (!select_stratified_coverage_window(spans.data(),
                                           spans.size(),
                                           coverage_window,
                                           kBehavioralMaxBytesSampled,
                                           kBehavioralCoverageStripeBytes,
                                           selection,
                                           slices)) {
        budget.aborted = true;
        budget.abort_reason = "coverage_window";
        error = "Coverage window index is outside the eligible inventory.";
        apply_coverage_selection(budget, selection);
        budget.elapsed_ms = static_cast<std::uint32_t>(tick_ms() - started);
        log_budget("BASELINE", budget);
        return false;
    }
    apply_coverage_selection(budget, selection);
    for (const CoverageSlice& slice : slices) {
        if (slice.size < 12) {
            continue;
        }
        StoredPage page{};
        page.base = slice.base;
        page.region_base = slice.region_base;
        page.bytes.resize(static_cast<std::size_t>(slice.size));
        if (!read_bytes(slice.base, page.bytes.data(), page.bytes.size())) {
            continue;
        }
        if (!consume_sample_bytes(budget, slice.size)) {
            error = "Sample byte budget exceeded.";
            budget.elapsed_ms = static_cast<std::uint32_t>(tick_ms() - started);
            log_budget("BASELINE", budget);
            return false;
        }
        pages.push_back(std::move(page));
    }
    budget.elapsed_ms = static_cast<std::uint32_t>(tick_ms() - started);
    log_budget("BASELINE", budget);
    return !budget.aborted;
}

} // namespace

RegionDecision classify_research_region(const RegionDescriptor& region) noexcept {
    if (region.size < 12) {
        return {false, "region_too_small"};
    }
    if (region.state != kResearchMemCommit) {
        return {false, "not_committed"};
    }
    if ((region.protect & kResearchPageGuard) != 0) {
        return {false, "guard"};
    }
    if ((region.protect & kResearchPageNoAccess) != 0 || region.protect == 0) {
        return {false, "noaccess"};
    }
    if (region.type == kResearchMemImage) {
        return {false, "image_static"};
    }
    if (region.type == kResearchMemMapped) {
        return {false, "mapped_file"};
    }
    if (region.type != kResearchMemPrivate) {
        return {false, "not_private"};
    }
    if (executable_protect(region.protect)) {
        return {false, "executable"};
    }
    if (!readable_protect(region.protect)) {
        return {false, "not_readable"};
    }
    return {true, "private_committed_readable"};
}

bool note_research_region(ScanBudget& budget, const RegionDescriptor& region) noexcept {
    if (budget.aborted) {
        return false;
    }
    ++budget.regions_considered;
    if (budget.regions_considered > kBehavioralMaxRegionsConsidered) {
        budget.aborted = true;
        budget.abort_reason = "region_consider_limit";
        return false;
    }
    const RegionDecision decision = classify_research_region(region);
    if (!decision.accept) {
        return true;
    }
    ++budget.regions_accepted;
    budget.bytes_considered += region.size;
    if (budget.regions_accepted > kBehavioralMaxRegionsAccepted) {
        budget.aborted = true;
        budget.abort_reason = "region_accept_limit";
        return false;
    }
    return true;
}

bool consume_sample_bytes(ScanBudget& budget, std::uint64_t bytes) noexcept {
    if (budget.aborted) {
        return false;
    }
    if (budget.bytes_sampled > kBehavioralMaxBytesSampled
        || bytes > kBehavioralMaxBytesSampled - budget.bytes_sampled) {
        budget.aborted = true;
        budget.abort_reason = "byte_limit";
        return false;
    }
    budget.bytes_sampled += bytes;
    return true;
}

bool plausible_position(Vec3 value) noexcept {
    const float components[3] = {value.x, value.y, value.z};
    for (float component : components) {
        if (!std::isfinite(component)) {
            return false;
        }
        if (std::fpclassify(component) == FP_SUBNORMAL) {
            return false;
        }
        if (std::fabs(component) > kBehavioralAbsurdMagnitude) {
            return false;
        }
    }
    return true;
}

float position_distance(Vec3 a, Vec3 b) noexcept {
    const float dx = a.x - b.x;
    const float dy = a.y - b.y;
    const float dz = a.z - b.z;
    return std::sqrt(dx * dx + dy * dy + dz * dz);
}

const char* behavioral_phase_at(BehavioralProtocol protocol, std::uint32_t index) noexcept {
    std::uint32_t count = 0;
    const char* const* phases = phases_for(protocol, count);
    if (index >= count) {
        return nullptr;
    }
    return phases[index];
}

std::uint32_t behavioral_phase_count(BehavioralProtocol protocol) noexcept {
    std::uint32_t count = 0;
    (void)phases_for(protocol, count);
    return count;
}

bool behavioral_phase_in_order(BehavioralProtocol protocol,
                               std::uint32_t sequence,
                               const char* phase_name) noexcept {
    if (sequence == 0 || phase_name == nullptr) {
        return false;
    }
    const char* expected = behavioral_phase_at(protocol, sequence - 1);
    return expected != nullptr && std::strcmp(expected, phase_name) == 0;
}

bool marker_matches_session(const PhaseMarker& marker, std::uint64_t session_nonce) noexcept {
    return marker.session_nonce == session_nonce && marker.session_nonce != 0 && marker.sequence != 0
        && !marker.phase.empty();
}

bool behavioral_phase_is_movement(const char* phase_name) noexcept {
    if (phase_name == nullptr) {
        return false;
    }
    return std::strncmp(phase_name, "MOVE_", 5) == 0 || std::strncmp(phase_name, "RUN_", 4) == 0
        || std::strcmp(phase_name, "TURN_LEFT") == 0;
}

std::uint32_t behavioral_phase_minimum_ms(const char* phase_name) noexcept {
    return behavioral_phase_is_movement(phase_name) ? kBehavioralMinActionMs : kBehavioralMinSettleMs;
}

PhaseIntervalStatus validate_phase_interval(const char* phase_name,
                                            std::uint64_t action_start_ms,
                                            std::uint64_t action_end_ms) noexcept {
    if (action_end_ms < action_start_ms) {
        return PhaseIntervalStatus::EndBeforeStart;
    }
    if (action_end_ms - action_start_ms < behavioral_phase_minimum_ms(phase_name)) {
        return PhaseIntervalStatus::TooShort;
    }
    return PhaseIntervalStatus::Accept;
}

bool phase_stage_follows(PhaseStage previous, PhaseStage next) noexcept {
    return (previous == PhaseStage::ActionStart && next == PhaseStage::ActionEnd)
        || (previous == PhaseStage::ActionEnd && next == PhaseStage::Capture);
}

bool candidate_observations_ready(const TransformCandidate& candidate) noexcept {
    return candidate.observations.size() >= 2;
}

bool positional_series_is_two_state(const TransformCandidate& candidate) noexcept {
    Vec3 distinct[9]{};
    std::uint32_t count = 0;
    for (const PhaseObservation& observation : candidate.observations) {
        if (!observation.present) {
            continue;
        }
        bool seen = false;
        for (std::uint32_t index = 0; index < count; ++index) {
            if (distinct[index].x == observation.value.x && distinct[index].y == observation.value.y
                && distinct[index].z == observation.value.z) {
                seen = true;
                break;
            }
        }
        if (!seen) {
            if (count >= 9) {
                return false;
            }
            distinct[count] = observation.value;
            ++count;
        }
    }
    return count < 3;
}

bool select_stratified_coverage_window(const EligibleSpan* spans,
                                       std::size_t span_count,
                                       std::uint32_t window_index,
                                       std::uint64_t byte_budget,
                                       std::uint32_t stripe_bytes,
                                       CoverageSelection& selection,
                                       std::vector<CoverageSlice>& slices) {
    selection = CoverageSelection{};
    selection.strategy_version = kBehavioralCoverageStrategyVersion;
    selection.window_index = window_index;
    selection.stripe_bytes = stripe_bytes;
    selection.addresses_comparable_across_processes = false;
    slices.clear();
    if (byte_budget == 0 || stripe_bytes == 0) {
        return false;
    }
    std::uint64_t total = 0;
    if (spans != nullptr) {
        for (std::size_t index = 0; index < span_count; ++index) {
            total += spans[index].size;
        }
    }
    selection.total_eligible_bytes = total;
    const std::uint64_t window_count = total == 0 ? 1 : (total + byte_budget - 1) / byte_budget;
    if (window_count > 0xffffffffull || window_index >= window_count) {
        selection.window_count = static_cast<std::uint32_t>(window_count > 0xffffffffull ? 0 : window_count);
        return false;
    }
    selection.window_count = static_cast<std::uint32_t>(window_count);
    std::uint64_t chunk_index = 0;
    bool saw_selected = false;
    bool truncated = false;
    for (std::size_t index = 0; spans != nullptr && index < span_count && !truncated; ++index) {
        const EligibleSpan& span = spans[index];
        bool span_selected = false;
        std::uint64_t offset = 0;
        while (offset < span.size) {
            const std::uint64_t chunk =
                span.size - offset < stripe_bytes ? span.size - offset : stripe_bytes;
            if (window_count == 0 || (chunk_index % window_count) == window_index) {
                if (selection.bytes_selected >= byte_budget) {
                    truncated = true;
                    break;
                }
                std::uint64_t take = chunk;
                if (take > byte_budget - selection.bytes_selected) {
                    take = byte_budget - selection.bytes_selected;
                    truncated = true;
                }
                const std::uint64_t address = span.base + offset;
                if (!slices.empty() && slices.back().region_base == span.base
                    && slices.back().base + slices.back().size == address) {
                    slices.back().size += take;
                } else {
                    slices.push_back(CoverageSlice{address, take, span.base});
                }
                if (!saw_selected) {
                    selection.window_start = address;
                    saw_selected = true;
                }
                selection.window_end = address + take;
                selection.bytes_selected += take;
                span_selected = true;
            } else if (!saw_selected) {
                selection.eligible_bytes_before_window += chunk;
            }
            offset += chunk;
            ++chunk_index;
            if (truncated) {
                break;
            }
        }
        if (span_selected) {
            ++selection.regions_selected;
        }
    }
    selection.partial = truncated || window_count > 1 || selection.bytes_selected < total;
    return true;
}

std::vector<TransformCandidate> extract_changed_position_triples(
    const std::uint8_t* before,
    const std::uint8_t* after,
    std::size_t size,
    std::uint64_t base_address,
    std::uint64_t region_base,
    ScanBudget& budget) {
    std::vector<TransformCandidate> found;
    if (before == nullptr || after == nullptr || size < 12 || budget.aborted) {
        return found;
    }
    struct Hit {
        std::size_t offset = 0;
        float displacement = 0.0f;
        Vec3 old_value{};
        Vec3 new_value{};
    };
    std::vector<Hit> hits;
    for (std::size_t offset = 0; offset + 12 <= size; offset += 4) {
        if (std::memcmp(before + offset, after + offset, 12) == 0) {
            continue;
        }
        Vec3 old_value{};
        Vec3 new_value{};
        std::memcpy(&old_value, before + offset, sizeof(old_value));
        std::memcpy(&new_value, after + offset, sizeof(new_value));
        ++budget.candidates_before_filter;
        if (!plausible_position(old_value) || !plausible_position(new_value)) {
            continue;
        }
        const float moved = position_distance(old_value, new_value);
        if (moved < kBehavioralMinMoveDistance || moved > kBehavioralMaxDisplacement) {
            continue;
        }
        Hit hit{};
        hit.offset = offset;
        hit.displacement = moved;
        hit.old_value = old_value;
        hit.new_value = new_value;
        if (!hits.empty() && offset < hits.back().offset + 12) {
            if (moved > hits.back().displacement) {
                hits.back() = hit;
            }
            continue;
        }
        hits.push_back(hit);
    }

    std::vector<std::uint32_t> page_counts;
    for (const Hit& hit : hits) {
        const std::size_t page = hit.offset / 4096u;
        if (page_counts.size() <= page) {
            page_counts.resize(page + 1, 0);
        }
        ++page_counts[page];
    }
    auto page_is_dense = [&page_counts](std::size_t page) {
        return page < page_counts.size() && page_counts[page] > kBehavioralMaxClustersPerPage;
    };
    std::vector<Hit> kept;
    for (const Hit& hit : hits) {
        // Overlapping stride-4 windows collapse onto the earliest anchor in the run.
        // That anchor can sit a few bytes before the structure and on the previous page.
        // Drop the cluster only when every page its 12 bytes touch is over the cap.
        const std::size_t first_page = hit.offset / 4096u;
        const std::size_t last_page = (hit.offset + 11) / 4096u;
        bool reject = true;
        for (std::size_t page = first_page; page <= last_page; ++page) {
            if (!page_is_dense(page)) {
                reject = false;
                break;
            }
        }
        if (!reject) {
            kept.push_back(hit);
        }
    }
    for (std::uint32_t count : page_counts) {
        if (count > kBehavioralMaxClustersPerPage) {
            ++budget.dense_pages_rejected;
        }
    }
    if (budget.candidates_after_filter > kBehavioralMaxCandidates
        || kept.size() > kBehavioralMaxCandidates - budget.candidates_after_filter) {
        budget.aborted = true;
        budget.abort_reason = "candidate_limit";
        return found;
    }
    for (const Hit& hit : kept) {
        TransformCandidate candidate{};
        candidate.address = base_address + static_cast<std::uint64_t>(hit.offset);
        candidate.region_base = region_base;
        PhaseObservation baseline{};
        baseline.phase = "BASELINE_STANDING";
        baseline.sequence = 1;
        baseline.value = hit.old_value;
        baseline.present = true;
        PhaseObservation moved_phase{};
        moved_phase.phase = "MOVE_FORWARD";
        moved_phase.sequence = 2;
        moved_phase.value = hit.new_value;
        moved_phase.present = true;
        candidate.observations.push_back(baseline);
        candidate.observations.push_back(moved_phase);
        found.push_back(std::move(candidate));
        ++budget.candidates_after_filter;
    }
    return found;
}

bool observe_candidate_phase(TransformCandidate& candidate,
                             const PhaseMarker& marker,
                             bool present,
                             Vec3 value) {
    if (!marker_matches_session(marker, marker.session_nonce)) {
        return false;
    }
    PhaseObservation observation{};
    observation.session_nonce = marker.session_nonce;
    observation.sequence = marker.sequence;
    observation.timestamp_ms = marker.timestamp_ms;
    observation.phase = marker.phase;
    observation.value = value;
    observation.present = present && plausible_position(value);
    if (present && !plausible_position(value)) {
        observation.present = false;
    }
    candidate.observations.push_back(std::move(observation));
    return true;
}

void classify_transform_candidate(TransformCandidate& candidate) {
    const char* required[] = {
        "BASELINE_STANDING",
        "MOVE_FORWARD",
        "STANDING_AFTER_FORWARD",
        "MOVE_BACKWARD",
        "STANDING_AFTER_BACKWARD",
        "TURN_LEFT",
        "STANDING_AFTER_TURN",
        "RUN_FORWARD",
        "FINAL_STANDING",
    };
    for (const char* phase : required) {
        if (latest(candidate, phase) == nullptr) {
            candidate.classification = CandidateClass::Transient;
            return;
        }
        if (!burst_stable(candidate, phase) && std::strncmp(phase, "MOVE_", 5) != 0
            && std::strncmp(phase, "RUN_", 4) != 0 && std::strcmp(phase, "TURN_LEFT") != 0) {
            candidate.classification = CandidateClass::Rejected;
            return;
        }
    }
    if (positional_series_is_two_state(candidate)) {
        candidate.classification = CandidateClass::Rejected;
        return;
    }
    const Vec3 baseline = latest(candidate, "BASELINE_STANDING")->value;
    const Vec3 forward = latest(candidate, "MOVE_FORWARD")->value;
    const Vec3 after_forward = latest(candidate, "STANDING_AFTER_FORWARD")->value;
    const Vec3 backward = latest(candidate, "MOVE_BACKWARD")->value;
    const Vec3 after_backward = latest(candidate, "STANDING_AFTER_BACKWARD")->value;
    const Vec3 turned = latest(candidate, "TURN_LEFT")->value;
    const Vec3 after_turn = latest(candidate, "STANDING_AFTER_TURN")->value;
    const Vec3 run = latest(candidate, "RUN_FORWARD")->value;
    const Vec3 final_standing = latest(candidate, "FINAL_STANDING")->value;
    const float walk = position_distance(baseline, forward);
    const float back = position_distance(after_forward, backward);
    const float turn = position_distance(after_backward, turned);
    const float run_distance = position_distance(after_turn, run);
    const bool stopped_after_forward =
        position_distance(forward, after_forward) <= kBehavioralStandingTolerance;
    const bool stopped_after_back =
        position_distance(backward, after_backward) <= kBehavioralStandingTolerance;
    const bool stopped_after_turn =
        position_distance(turned, after_turn) <= kBehavioralStandingTolerance;
    const bool stopped_final =
        position_distance(run, final_standing) <= kBehavioralStandingTolerance;
    const bool reversed = dot_delta(baseline, forward, after_forward, backward) < 0.0f;
    const bool walked = walk >= kBehavioralMinMoveDistance && walk <= kBehavioralMaxDisplacement;
    const bool ran_farther = run_distance > walk;
    const bool turned_in_place = turn <= kBehavioralStandingTolerance;
    const PhaseObservation* camera = latest(candidate, "PLAYER_STILL_CAMERA_ROTATE");
    if (camera != nullptr && position_distance(final_standing, camera->value) > kBehavioralStandingTolerance) {
        candidate.classification = CandidateClass::CameraLike;
        return;
    }
    if (walked && reversed && stopped_after_forward && stopped_after_back && turned_in_place
        && stopped_after_turn && ran_farther && stopped_final && back >= kBehavioralMinMoveDistance) {
        candidate.classification = CandidateClass::BehavioralStrong;
        return;
    }
    if (!turned_in_place && walked && reversed) {
        candidate.classification = CandidateClass::CameraLike;
        return;
    }
    if (walked && reversed && stopped_final) {
        candidate.classification = CandidateClass::BehavioralModerate;
        return;
    }
    if (walked) {
        candidate.classification = CandidateClass::BehavioralWeak;
        return;
    }
    candidate.classification = CandidateClass::Rejected;
}

BehaviorSignature behavior_signature(const TransformCandidate& candidate) noexcept {
    BehaviorSignature signature{};
    const PhaseObservation* baseline = latest(candidate, "BASELINE_STANDING");
    const PhaseObservation* forward = latest(candidate, "MOVE_FORWARD");
    const PhaseObservation* after_forward = latest(candidate, "STANDING_AFTER_FORWARD");
    const PhaseObservation* backward = latest(candidate, "MOVE_BACKWARD");
    const PhaseObservation* after_backward = latest(candidate, "STANDING_AFTER_BACKWARD");
    const PhaseObservation* turned = latest(candidate, "TURN_LEFT");
    const PhaseObservation* after_turn = latest(candidate, "STANDING_AFTER_TURN");
    const PhaseObservation* run = latest(candidate, "RUN_FORWARD");
    if (baseline == nullptr || forward == nullptr) {
        return signature;
    }
    signature.walk_distance = position_distance(baseline->value, forward->value);
    signature.valid = true;
    if (after_forward != nullptr && backward != nullptr) {
        signature.backward_alignment =
            dot_delta(baseline->value, forward->value, after_forward->value, backward->value);
    }
    if (after_backward != nullptr && turned != nullptr) {
        signature.turn_distance = position_distance(after_backward->value, turned->value);
    }
    if (after_turn != nullptr && run != nullptr) {
        signature.run_distance = position_distance(after_turn->value, run->value);
    }
    return signature;
}

CrossSessionCorrelation correlate_behavior_across_sessions(
    const std::vector<BehaviorSignature>& previous,
    const std::vector<BehaviorSignature>& current) noexcept {
    int matches = 0;
    for (const BehaviorSignature& left : previous) {
        if (!left.valid) {
            continue;
        }
        for (const BehaviorSignature& right : current) {
            if (!right.valid) {
                continue;
            }
            const float walk_scale = left.walk_distance > 1.0f ? left.walk_distance : 1.0f;
            const bool walk_close =
                std::fabs(left.walk_distance - right.walk_distance) <= walk_scale * 0.5f;
            const bool reversal_agrees =
                (left.backward_alignment < 0.0f && right.backward_alignment < 0.0f)
                || (left.backward_alignment == 0.0f && right.backward_alignment == 0.0f);
            const bool turn_agrees =
                (left.turn_distance <= kBehavioralStandingTolerance
                 && right.turn_distance <= kBehavioralStandingTolerance)
                || (left.turn_distance > kBehavioralStandingTolerance
                    && right.turn_distance > kBehavioralStandingTolerance);
            if (walk_close && reversal_agrees && turn_agrees) {
                ++matches;
            }
        }
    }
    if (matches == 1) {
        return CrossSessionCorrelation::UniqueBehavioralMatch;
    }
    if (matches > 1) {
        return CrossSessionCorrelation::Ambiguous;
    }
    return CrossSessionCorrelation::NoMatch;
}

const char* to_string(CandidateClass classification) noexcept {
    switch (classification) {
    case CandidateClass::Unclassified:
        return "UNCLASSIFIED";
    case CandidateClass::BehavioralStrong:
        return "BEHAVIORAL_STRONG";
    case CandidateClass::BehavioralModerate:
        return "BEHAVIORAL_MODERATE";
    case CandidateClass::BehavioralWeak:
        return "BEHAVIORAL_WEAK";
    case CandidateClass::CameraLike:
        return "CAMERA_LIKE";
    case CandidateClass::Transient:
        return "TRANSIENT";
    case CandidateClass::Rejected:
        return "REJECTED";
    }
    return "UNKNOWN";
}

bool publish_behavioral_phase_marker(const PhaseMarker& marker, std::string& message) noexcept {
    if (!marker_matches_session(marker, marker.session_nonce)) {
        message = "Phase marker is missing nonce, sequence, or name.";
        return false;
    }
    wchar_t name[96]{};
    mapping_name(L"Local\\DeFlux_11A_Phase_", marker.session_nonce, name, 96);
    static HANDLE held_mapping = nullptr;
    if (held_mapping == nullptr) {
        held_mapping = CreateFileMappingW(INVALID_HANDLE_VALUE,
                                          nullptr,
                                          PAGE_READWRITE,
                                          0,
                                          sizeof(PhaseIpcRecord),
                                          name);
    }
    HANDLE mapping = held_mapping;
    if (mapping == nullptr) {
        message = "CreateFileMappingW for the phase marker failed.";
        return false;
    }
    auto* view = static_cast<PhaseIpcRecord*>(
        MapViewOfFile(mapping, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(PhaseIpcRecord)));
    if (view == nullptr) {
        message = "MapViewOfFile for the phase marker failed.";
        return false;
    }
    PhaseIpcRecord record{};
    record.magic = kPhaseMagic;
    record.sequence = marker.sequence;
    record.session_nonce = marker.session_nonce;
    record.timestamp_ms = marker.timestamp_ms != 0 ? marker.timestamp_ms : tick_ms();
    record.protocol = static_cast<std::uint32_t>(marker.protocol);
    record.stage = static_cast<std::uint32_t>(marker.stage);
    record.coverage_window = marker.coverage_window;
    strncpy_s(record.phase, marker.phase.c_str(), _TRUNCATE);
    *view = record;
    UnmapViewOfFile(view);
    message = "Phase marker published.";
    return true;
}

bool wait_for_behavioral_phase_ack(std::uint64_t session_nonce,
                                   std::uint32_t sequence,
                                   PhaseStage stage,
                                   std::uint32_t timeout_ms,
                                   std::string& message) noexcept {
    wchar_t name[96]{};
    mapping_name(L"Local\\DeFlux_11A_PhaseAck_", session_nonce, name, 96);
    const std::uint64_t deadline = tick_ms() + timeout_ms;
    while (tick_ms() < deadline) {
        HANDLE mapping = OpenFileMappingW(FILE_MAP_READ, FALSE, name);
        if (mapping == nullptr) {
            Sleep(20);
            continue;
        }
        const auto* view = static_cast<const AckIpcRecord*>(
            MapViewOfFile(mapping, FILE_MAP_READ, 0, 0, sizeof(AckIpcRecord)));
        if (view == nullptr) {
            CloseHandle(mapping);
            Sleep(20);
            continue;
        }
        AckIpcRecord record = *view;
        UnmapViewOfFile(view);
        CloseHandle(mapping);
        if (record.magic == kAckMagic && record.session_nonce == session_nonce
            && record.sequence == sequence && record.stage == static_cast<std::uint32_t>(stage)) {
            message = record.message;
            return record.ok != 0;
        }
        Sleep(20);
    }
    message = "Timed out waiting for the phase acknowledgement.";
    return false;
}

void run_behavioral_transform_session(std::uint64_t session_nonce,
                                      std::uint32_t process_id,
                                      std::uint64_t creation_identity,
                                      const char* expected_sha256,
                                      const char* observed_sha256) noexcept {
    try {
        ResearchEvidenceSessionHeader header{};
        header.process_id = process_id;
        header.creation_identity = creation_identity;
        header.session_nonce = session_nonce;
        header.experiment_id = kBehavioralTransformExperimentId;
        header.experiment_version = 1;
        header.expected_sha256 = expected_sha256 != nullptr ? expected_sha256 : "";
        header.observed_sha256 = observed_sha256 != nullptr ? observed_sha256 : "";
        header.pipeline_proof = false;
        research_evidence_set_hook_enabled(true);
        std::string log_path;
        if (!research_evidence_begin_session(header, log_path)) {
            return;
        }
        research_evidence_append_lifecycle("BEHAVIORAL_SESSION_START", kBehavioralTransformExperimentId);

        auto fail_session = [&](std::uint32_t sequence, PhaseStage stage, const char* reason) {
            research_evidence_append_lifecycle("BEHAVIORAL_ABORT", reason);
            (void)publish_ack(session_nonce, sequence, stage, false, reason);
            research_evidence_end_session(ResearchSubstate::Failed,
                                          InProcessSignalClassification::None,
                                          reason);
        };

        PhaseMarker started{};
        std::string error;
        if (!wait_marker(session_nonce, 1, PhaseStage::ActionStart, BehavioralProtocol::Primary, started, error)) {
            fail_session(1, PhaseStage::ActionStart, error.c_str());
            return;
        }
        const BehavioralProtocol protocol = started.protocol;
        const std::uint32_t coverage_window = started.coverage_window;
        research_evidence_append_lifecycle("ACTION_START", started.phase.c_str());
        (void)publish_ack(session_nonce, 1, PhaseStage::ActionStart, true, "action_started");

        PhaseMarker baseline_end{};
        if (!wait_marker(session_nonce, 1, PhaseStage::ActionEnd, protocol, baseline_end, error)) {
            fail_session(1, PhaseStage::Capture, error.c_str());
            return;
        }
        const PhaseIntervalStatus baseline_interval =
            validate_phase_interval(baseline_end.phase.c_str(), started.timestamp_ms, baseline_end.timestamp_ms);
        if (baseline_interval != PhaseIntervalStatus::Accept) {
            const char* reason = baseline_interval == PhaseIntervalStatus::EndBeforeStart
                ? "action_end_before_start"
                : "action_too_short";
            research_evidence_append_lifecycle("PHASE_REJECTED", reason);
            fail_session(1, PhaseStage::Capture, reason);
            return;
        }
        research_evidence_append_lifecycle("ACTION_END", baseline_end.phase.c_str());

        ScanBudget budget{};
        std::vector<StoredPage> pages;
        research_evidence_append_lifecycle("CAPTURE", baseline_end.phase.c_str());
        if (!capture_baseline(budget, pages, coverage_window, error)) {
            fail_session(1, PhaseStage::Capture, error.c_str());
            return;
        }
        (void)publish_ack(session_nonce, 1, PhaseStage::Capture, true, "capture_ack");

        std::vector<TransformCandidate> candidates;
        const std::uint32_t count = behavioral_phase_count(protocol);
        for (std::uint32_t sequence = 2; sequence <= count; ++sequence) {
            PhaseMarker action_start{};
            if (!wait_marker(session_nonce, sequence, PhaseStage::ActionStart, protocol, action_start, error)) {
                fail_session(sequence, PhaseStage::ActionStart, error.c_str());
                return;
            }
            research_evidence_append_lifecycle("ACTION_START", action_start.phase.c_str());
            (void)publish_ack(session_nonce, sequence, PhaseStage::ActionStart, true, "action_started");

            PhaseMarker marker{};
            if (!wait_marker(session_nonce, sequence, PhaseStage::ActionEnd, protocol, marker, error)) {
                fail_session(sequence, PhaseStage::Capture, error.c_str());
                return;
            }
            const PhaseIntervalStatus interval = validate_phase_interval(
                marker.phase.c_str(), action_start.timestamp_ms, marker.timestamp_ms);
            if (interval != PhaseIntervalStatus::Accept) {
                const char* reason = interval == PhaseIntervalStatus::EndBeforeStart ? "action_end_before_start"
                                                                                     : "action_too_short";
                research_evidence_append_lifecycle("PHASE_REJECTED", reason);
                fail_session(sequence, PhaseStage::Capture, reason);
                return;
            }
            research_evidence_append_lifecycle("ACTION_END", marker.phase.c_str());
            research_evidence_append_lifecycle("CAPTURE", marker.phase.c_str());
            if (sequence == 2) {
                const std::uint64_t capture_started = tick_ms();
                for (StoredPage& page : pages) {
                    std::vector<std::uint8_t> current(page.bytes.size());
                    if (!read_bytes(page.base, current.data(), current.size())) {
                        continue;
                    }
                    auto extracted = extract_changed_position_triples(page.bytes.data(),
                                                                      current.data(),
                                                                      current.size(),
                                                                      page.base,
                                                                      page.region_base,
                                                                      budget);
                    for (TransformCandidate& candidate : extracted) {
                        if (!candidate_observations_ready(candidate)) {
                            budget.aborted = true;
                            budget.abort_reason = "incomplete_candidate";
                            break;
                        }
                        candidate.observations[0].session_nonce = session_nonce;
                        candidate.observations[0].timestamp_ms = baseline_end.timestamp_ms;
                        candidate.observations[1].session_nonce = session_nonce;
                        candidate.observations[1].timestamp_ms = marker.timestamp_ms;
                        candidates.push_back(std::move(candidate));
                    }
                    if (budget.aborted) {
                        break;
                    }
                    page.bytes.clear();
                    page.bytes.shrink_to_fit();
                }
                pages.clear();
                budget.elapsed_ms = static_cast<std::uint32_t>(tick_ms() - capture_started);
                log_budget("MOVE_FORWARD", budget);
                if (budget.aborted) {
                    fail_session(sequence,
                                 PhaseStage::Capture,
                                 budget.abort_reason != nullptr ? budget.abort_reason : "capture_failed");
                    return;
                }
            } else {
                // A missing sample stays on the candidate as present=0. It is not deleted here.
                // Classification may call that candidate TRANSIENT after the full series is recorded.
                for (TransformCandidate& candidate : candidates) {
                    Vec3 value{};
                    const bool present = read_bytes(candidate.address, &value, sizeof(value));
                    (void)observe_candidate_phase(candidate, marker, present, value);
                }
            }
            (void)publish_ack(session_nonce, sequence, PhaseStage::Capture, true, "capture_ack");
        }

        for (TransformCandidate& candidate : candidates) {
            classify_transform_candidate(candidate);
            if (candidate.classification == CandidateClass::Rejected
                || candidate.classification == CandidateClass::Unclassified) {
                continue;
            }
            for (const PhaseObservation& observation : candidate.observations) {
                log_candidate(candidate, observation);
            }
        }
        budget.candidates_after_filter = static_cast<std::uint32_t>(candidates.size());
        log_budget("CLASSIFIED", budget);
        research_evidence_append_lifecycle("BEHAVIORAL_SESSION_END", "operator_phases_complete");
        research_evidence_end_session(ResearchSubstate::Stopped,
                                      InProcessSignalClassification::None,
                                      "Behavioral transform session completed.");
    } catch (const std::exception& ex) {
        research_evidence_append_lifecycle("BEHAVIORAL_ABORT", ex.what());
    } catch (...) {
        research_evidence_append_lifecycle("BEHAVIORAL_ABORT", "unknown");
    }
}

int run_behavioral_phase_console(std::uint64_t session_nonce,
                                 BehavioralProtocol protocol,
                                 std::uint32_t coverage_window) noexcept {
    const std::uint32_t count = behavioral_phase_count(protocol);
    std::printf("DeFlux Task #11B phase console\n");
    std::printf("session_nonce=%llu\n", static_cast<unsigned long long>(session_nonce));
    std::printf("protocol=%s\n", protocol == BehavioralProtocol::Validation ? "validation" : "primary");
    std::printf("coverage_window=%u\n", coverage_window);
    std::printf("Avoid vehicles, combat, jumping, missions, menus, pause, and NPC interaction.\n");
    auto read_enter = []() noexcept {
        for (int ch = std::fgetc(stdin); ch != '\n' && ch != EOF; ch = std::fgetc(stdin)) {
        }
        return !std::feof(stdin);
    };
    for (std::uint32_t sequence = 1; sequence <= count; ++sequence) {
        const char* phase = behavioral_phase_at(protocol, sequence - 1);
        const std::uint32_t minimum_ms = behavioral_phase_minimum_ms(phase);
        std::printf("\n%u. READY_FOR_PHASE %s\n", sequence, phase);
        std::printf("Press Enter to start. Minimum interval %u ms.\n", minimum_ms);
        if (!read_enter()) {
            return 1;
        }
        PhaseMarker start{};
        start.session_nonce = session_nonce;
        start.sequence = sequence;
        start.timestamp_ms = tick_ms();
        start.protocol = protocol;
        start.stage = PhaseStage::ActionStart;
        start.coverage_window = coverage_window;
        start.phase = phase;
        std::string message;
        if (!publish_behavioral_phase_marker(start, message)) {
            std::printf("marker failed: %s\n", message.c_str());
            return 1;
        }
        if (!wait_for_behavioral_phase_ack(session_nonce,
                                           sequence,
                                           PhaseStage::ActionStart,
                                           kBehavioralMaxCaptureMs + 5000,
                                           message)) {
            std::printf("phase error: %s\n", message.c_str());
            return 1;
        }
        std::printf("ACTION_ACTIVE %s. Press Enter when the action is finished.\n", phase);
        if (!read_enter()) {
            return 1;
        }
        PhaseMarker end{};
        end.session_nonce = session_nonce;
        end.sequence = sequence;
        end.timestamp_ms = tick_ms();
        end.protocol = protocol;
        end.stage = PhaseStage::ActionEnd;
        end.coverage_window = coverage_window;
        end.phase = phase;
        if (!publish_behavioral_phase_marker(end, message)) {
            std::printf("marker failed: %s\n", message.c_str());
            return 1;
        }
        if (!wait_for_behavioral_phase_ack(session_nonce,
                                           sequence,
                                           PhaseStage::Capture,
                                           kBehavioralMaxCaptureMs + 5000,
                                           message)) {
            std::printf("phase error: %s\n", message.c_str());
            return 1;
        }
        std::printf("CAPTURE_ACK %s (%s)\n", phase, message.c_str());
    }
    std::printf("\nPrimary phases are complete. Exit GTA normally when you are finished.\n");
    return 0;
}

} // namespace deflux::game::gta_sa_de
