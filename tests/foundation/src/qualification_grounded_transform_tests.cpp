#include "deflux/game/gta_sa_de/grounded_transform_research.hpp"
#include "deflux/game/gta_sa_de/grounded_live_research.hpp"
#include "deflux/game/gta_sa_de/research_launcher.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <limits>
#include <string>
#include <string_view>
#include <vector>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>

namespace deflux_tests {
namespace {

int g_passed = 0;
int g_failed = 0;

void check(bool condition, const char* expression, int line) {
    if (condition) {
        ++g_passed;
        return;
    }
    ++g_failed;
    std::printf("FAIL %s:%d: %s\n", __FILE__, line, expression);
}

#define CHECK_GT(condition) check(static_cast<bool>(condition), #condition, __LINE__)

using deflux::game::gta_sa_de::ClassifiedAddress;
using deflux::game::gta_sa_de::Float64Diagnostic;
using deflux::game::gta_sa_de::GroundedEvidence;
using deflux::game::gta_sa_de::GroundedHit;
using deflux::game::gta_sa_de::GroundedModule;
using deflux::game::gta_sa_de::GroundedModuleKind;
using deflux::game::gta_sa_de::GroundedObservation;
using deflux::game::gta_sa_de::GroundedQualification;
using deflux::game::gta_sa_de::GroundedQualifyRequest;
using deflux::game::gta_sa_de::GroundedRegion;
using deflux::game::gta_sa_de::GroundedSlot;
using deflux::game::gta_sa_de::GroundedVec3;
using deflux::game::gta_sa_de::GroundedView;
using deflux::game::gta_sa_de::OracleLogWatcher;
using deflux::game::gta_sa_de::OracleSample;
using deflux::game::gta_sa_de::RecheckRequest;
using deflux::game::gta_sa_de::ScanClock;
using deflux::game::gta_sa_de::StreamScanRequest;
using deflux::game::gta_sa_de::StreamScanResult;
using deflux::game::gta_sa_de::classify_grounded_observation;
using deflux::game::gta_sa_de::evaluate_grounded_live_attach;
using deflux::game::gta_sa_de::grounded_buffer_contains_marker;
using deflux::game::gta_sa_de::grounded_candidate_component_absurd;
using deflux::game::gta_sa_de::grounded_copy_triple;
using deflux::game::gta_sa_de::grounded_displacement_agrees;
using deflux::game::gta_sa_de::grounded_double_read_stable;
using deflux::game::gta_sa_de::grounded_exact_match;
using deflux::game::gta_sa_de::grounded_is_stale;
using deflux::game::gta_sa_de::grounded_live_requirements_met;
using deflux::game::gta_sa_de::grounded_near_match;
using deflux::game::gta_sa_de::grounded_observation_tick;
using deflux::game::gta_sa_de::grounded_offset_holds;
using deflux::game::gta_sa_de::grounded_oracle_invalid;
using deflux::game::gta_sa_de::grounded_phase_count;
using deflux::game::gta_sa_de::grounded_phase_duration_ok;
using deflux::game::gta_sa_de::grounded_phase_is_standing;
using deflux::game::gta_sa_de::grounded_phase_name;
using deflux::game::gta_sa_de::grounded_process_identity_holds;
using deflux::game::gta_sa_de::grounded_region_eligible;
using deflux::game::gta_sa_de::grounded_select_offset;
using deflux::game::gta_sa_de::grounded_session_claim;
using deflux::game::gta_sa_de::grounded_standing_median;
using deflux::game::gta_sa_de::grounded_standing_oracle_stable;
using deflux::game::gta_sa_de::grounded_standing_positions_separated;
using deflux::game::gta_sa_de::grounded_turn_stable;
using deflux::game::gta_sa_de::grounded_vehicle_aborts_session;
using deflux::game::gta_sa_de::kGroundedCandidateAbsurd;
using deflux::game::gta_sa_de::kGroundedDirectionCosine;
using deflux::game::gta_sa_de::kGroundedExactMeters;
using deflux::game::gta_sa_de::kGroundedFullScanMs;
using deflux::game::gta_sa_de::kGroundedLiveClaimWithheld;
using deflux::game::gta_sa_de::kGroundedMaxBytesConsidered;
using deflux::game::gta_sa_de::kGroundedMaxCandidates;
using deflux::game::gta_sa_de::kGroundedNearMeters;
using deflux::game::gta_sa_de::kGroundedOffsetZ;
using deflux::game::gta_sa_de::kGroundedOraclePeriodMs;
using deflux::game::gta_sa_de::kGroundedRecheckMs;
using deflux::game::gta_sa_de::kGroundedStandingWindowMs;
using deflux::game::gta_sa_de::kGroundedWatcherPollMs;
using deflux::game::gta_sa_de::qualify_grounded_session;
using deflux::game::gta_sa_de::recheck_grounded_hits;
using deflux::game::gta_sa_de::stream_scan_float32;
using deflux::game::gta_sa_de::to_string;

constexpr std::uint32_t kCommit = 0x1000u;
constexpr std::uint32_t kPrivate = 0x20000u;
constexpr std::uint32_t kMapped = 0x40000u;
constexpr std::uint32_t kImage = 0x1000000u;
constexpr std::uint32_t kNoAccess = 0x01u;
constexpr std::uint32_t kReadWrite = 0x04u;
constexpr std::uint32_t kExecuteRead = 0x20u;
constexpr std::uint32_t kGuard = 0x100u;
constexpr std::uint64_t kPage = 4096;
constexpr std::uint64_t kArena = 0x20000000ull;

const GroundedVec3 kA{100.0, 200.0, 10.0};
const GroundedVec3 kC{100.0, 250.0, 10.0};
const GroundedVec3 kE{180.0, 250.0, 12.0};
const GroundedVec3 kH{180.0, 320.0, 15.0};

enum PageId {
    PageTrue = 0,
    PageNear,
    PagePlus,
    PageMinus,
    PageArbitrary,
    PageStale,
    PageCamera,
    PagePhysics,
    PageDense,
    PageBoundaryLo,
    PageBoundaryHi,
    PageInaccessibleLo,
    PageInaccessibleHi,
    PageFailC,
    PageFloat64,
    PageMarker,
    PageNan,
    PageInf,
    PageAbsurd,
    PageCleo,
    PagePlugin,
    PageResearchModule,
    PageResearchBuffer,
    PageGameImage,
    PageSilence,
    PageGuard,
    PageCount,
};

struct Page {
    std::uint64_t base = 0;
    std::uint32_t protect = kReadWrite;
    std::uint32_t type = kPrivate;
    bool present = true;
    std::vector<std::uint8_t> bytes;
};

struct Fixture {
    std::vector<Page> pages;
    std::vector<GroundedRegion> regions;
    std::vector<GroundedModule> modules;

    std::uint64_t address(int page, std::uint64_t offset) const {
        return pages[static_cast<std::size_t>(page)].base + offset;
    }

    void write_f32(std::uint64_t address, float value) {
        for (Page& page : pages) {
            if (!page.present || address < page.base || address - page.base >= page.bytes.size()) {
                continue;
            }
            std::memcpy(page.bytes.data() + (address - page.base), &value, sizeof(value));
            return;
        }
    }

    void write_xyz(std::uint64_t address, GroundedVec3 value) {
        write_f32(address, static_cast<float>(value.x));
        write_f32(address + 4, static_cast<float>(value.y));
        write_f32(address + 8, static_cast<float>(value.z));
    }

    void write_f64(std::uint64_t address, double value) {
        for (Page& page : pages) {
            if (!page.present || address < page.base || address + 8 - page.base > page.bytes.size()) {
                continue;
            }
            std::memcpy(page.bytes.data() + (address - page.base), &value, sizeof(value));
            return;
        }
    }
};

Fixture make_fixture() {
    Fixture fixture{};
    fixture.pages.resize(PageCount);
    for (int index = 0; index < PageCount; ++index) {
        Page& page = fixture.pages[static_cast<std::size_t>(index)];
        page.base = kArena + static_cast<std::uint64_t>(index) * kPage;
        page.bytes.assign(static_cast<std::size_t>(kPage), 0);
    }
    fixture.pages[PageInaccessibleHi].present = false;
    fixture.pages[PageInaccessibleHi].protect = kNoAccess;
    fixture.pages[PageInaccessibleHi].bytes.clear();
    fixture.pages[PageGuard].protect = kReadWrite | kGuard;
    fixture.pages[PageCleo].type = kImage;
    fixture.pages[PagePlugin].type = kImage;
    fixture.pages[PageResearchModule].type = kImage;
    fixture.pages[PageGameImage].type = kImage;
    std::memcpy(fixture.pages[PageMarker].bytes.data(), "DEFLUX14", 8);
    std::memcpy(fixture.pages[PageGameImage].bytes.data(), "DEFLUX14", 8);

    fixture.regions.reserve(PageCount);
    for (const Page& page : fixture.pages) {
        GroundedRegion region{};
        region.base = page.base;
        region.size = kPage;
        region.state = kCommit;
        region.protect = page.protect;
        region.type = page.type;
        region.bytes = page.present ? page.bytes.data() : nullptr;
        fixture.regions.push_back(region);
    }
    fixture.modules = {
        {fixture.address(PageCleo, 0), kPage, GroundedModuleKind::CleoAsi},
        {fixture.address(PagePlugin, 0), kPage, GroundedModuleKind::CleoPlugin},
        {fixture.address(PageResearchModule, 0), kPage, GroundedModuleKind::ResearchModule},
        {fixture.address(PageResearchBuffer, 0), kPage, GroundedModuleKind::ResearchBuffer},
        {fixture.address(PageGameImage, 0), kPage, GroundedModuleKind::GameImage},
    };
    return fixture;
}

GroundedVec3 phase_xyz(std::uint32_t slot) {
    if (slot == static_cast<std::uint32_t>(GroundedSlot::StandA1)
        || slot == static_cast<std::uint32_t>(GroundedSlot::StandA2)) {
        return kA;
    }
    if (slot == static_cast<std::uint32_t>(GroundedSlot::StandC1)
        || slot == static_cast<std::uint32_t>(GroundedSlot::StandC2)) {
        return kC;
    }
    if (slot == static_cast<std::uint32_t>(GroundedSlot::StandE1)
        || slot == static_cast<std::uint32_t>(GroundedSlot::StandE2)
        || slot == static_cast<std::uint32_t>(GroundedSlot::TurnF)) {
        return kE;
    }
    return kH;
}

GroundedVec3 shifted(GroundedVec3 value, double x, double y, double z) {
    return {value.x + x, value.y + y, value.z + z};
}

void prepare_fixture(void* context, std::uint32_t slot) {
    auto* fixture = static_cast<Fixture*>(context);
    const GroundedVec3 here = phase_xyz(slot);
    fixture->write_xyz(fixture->address(PageTrue, 64), here);
    fixture->write_xyz(fixture->address(PageNear, 64), shifted(here, 0.03, 0.0, 0.0));
    fixture->write_xyz(fixture->address(PagePlus, 64), shifted(here, 0.0, 0.0, kGroundedOffsetZ));
    fixture->write_xyz(fixture->address(PageMinus, 64), shifted(here, 0.0, 0.0, -kGroundedOffsetZ));
    fixture->write_xyz(fixture->address(PageArbitrary, 64), shifted(here, 5.0, 0.0, 0.0));
    fixture->write_xyz(fixture->address(PageStale, 64), kA);
    fixture->write_xyz(fixture->address(PageCamera, 64),
                          slot == static_cast<std::uint32_t>(GroundedSlot::TurnF) ? shifted(kE, 2.0, 0.0, 0.0) : here);
    fixture->write_xyz(fixture->address(PagePhysics, 64),
                       slot == static_cast<std::uint32_t>(GroundedSlot::StandA2) ? shifted(kA, 1.0, 0.0, 0.0) : here);
    for (int copy = 0; copy < 9; ++copy) {
        fixture->write_xyz(fixture->address(PageDense, 64 + static_cast<std::uint64_t>(copy) * 16), here);
    }
    fixture->write_xyz(fixture->address(PageBoundaryLo, kPage - 8), here);
    fixture->write_xyz(fixture->address(PageFailC, 64),
                       slot <= static_cast<std::uint32_t>(GroundedSlot::StandA2) ? kA : GroundedVec3{400.0, 400.0, 10.0});
    fixture->write_f64(fixture->address(PageFloat64, 64), here.x);
    fixture->write_f64(fixture->address(PageFloat64, 72), here.y);
    fixture->write_f64(fixture->address(PageFloat64, 80), here.z);
    fixture->write_xyz(fixture->address(PageMarker, 128), here);
    const float nan = std::nanf("");
    const float inf = std::numeric_limits<float>::infinity();
    fixture->write_f32(fixture->address(PageNan, 64), nan);
    fixture->write_f32(fixture->address(PageNan, 68), nan);
    fixture->write_f32(fixture->address(PageNan, 72), nan);
    fixture->write_f32(fixture->address(PageInf, 64), inf);
    fixture->write_f32(fixture->address(PageInf, 68), inf);
    fixture->write_f32(fixture->address(PageInf, 72), inf);
    fixture->write_xyz(fixture->address(PageAbsurd, 64), {1.0e7, 1.0e7, 1.0e7});
    fixture->write_xyz(fixture->address(PageCleo, 64), here);
    fixture->write_xyz(fixture->address(PagePlugin, 64), here);
    fixture->write_xyz(fixture->address(PageResearchModule, 64), here);
    fixture->write_xyz(fixture->address(PageResearchBuffer, 64), here);
    fixture->write_xyz(fixture->address(PageGameImage, 128), kA);
    fixture->write_xyz(fixture->address(PageSilence, 64),
                         slot == static_cast<std::uint32_t>(GroundedSlot::Silence) ? GroundedVec3{0.0, 0.0, 0.0} : here);
    fixture->write_xyz(fixture->address(PageGuard, 64), here);
    fixture->write_xyz(fixture->address(PageInaccessibleLo, kPage - 8), here);
}

GroundedQualification run_fixture(Fixture& fixture) {
    GroundedQualifyRequest request{};
    request.prepare = prepare_fixture;
    request.context = &fixture;
    request.view.regions = fixture.regions.data();
    request.view.region_count = fixture.regions.size();
    request.view.modules = fixture.modules.data();
    request.view.module_count = fixture.modules.size();
    request.oracle_a = kA;
    request.oracle_c = kC;
    request.oracle_e = kE;
    request.oracle_h = kH;
    return qualify_grounded_session(request);
}

const ClassifiedAddress* find_address(const GroundedQualification& result, std::uint64_t address) {
    for (const ClassifiedAddress& item : result.addresses) {
        if (item.address == address) {
            return &item;
        }
    }
    return nullptr;
}

bool evidence_is(const GroundedQualification& result, std::uint64_t address, GroundedEvidence evidence) {
    const ClassifiedAddress* item = find_address(result, address);
    return item != nullptr && item->evidence == evidence;
}

void test_match_boundaries() {
    const GroundedVec3 oracle{0.0, 0.0, 0.0};
    CHECK_GT(grounded_exact_match({kGroundedExactMeters, 0.0, 0.0}, oracle));
    CHECK_GT(!grounded_exact_match({std::nextafter(kGroundedExactMeters, 1.0), 0.0, 0.0}, oracle));
    CHECK_GT(grounded_near_match({kGroundedNearMeters, 0.0, 0.0}, oracle));
    CHECK_GT(!grounded_near_match({std::nextafter(kGroundedNearMeters, 1.0), 0.0, 0.0}, oracle));
    CHECK_GT(!grounded_exact_match({kGroundedNearMeters, 0.0, 0.0}, oracle));
    CHECK_GT(!grounded_near_match({0.2, 0.0, 0.0}, oracle));

    GroundedVec3 offset{};
    const GroundedVec3 placed{10.0, 20.0, 30.0};
    CHECK_GT(grounded_select_offset(placed, placed, offset));
    CHECK_GT(offset.x == 0.0 && offset.y == 0.0 && offset.z == 0.0);
    CHECK_GT(grounded_select_offset({10.0, 20.0, 30.0 + kGroundedOffsetZ}, placed, offset));
    CHECK_GT(offset.z == kGroundedOffsetZ);
    CHECK_GT(grounded_select_offset({10.0, 20.0, 30.0 - kGroundedOffsetZ}, placed, offset));
    CHECK_GT(offset.z == -kGroundedOffsetZ);
    CHECK_GT(!grounded_select_offset({15.0, 20.0, 30.0}, placed, offset));
    CHECK_GT(grounded_offset_holds({10.0, 20.0, 30.0 + kGroundedOffsetZ}, placed, {0.0, 0.0, kGroundedOffsetZ}));
    CHECK_GT(!grounded_offset_holds({15.0, 20.0, 30.0}, placed, {0.0, 0.0, 0.0}));

    CHECK_GT(grounded_candidate_component_absurd(kGroundedCandidateAbsurd + 1.0));
    CHECK_GT(!grounded_candidate_component_absurd(kGroundedCandidateAbsurd));
    CHECK_GT(grounded_oracle_invalid({100001.0, 0.0, 0.0}));
    CHECK_GT(!grounded_oracle_invalid({100000.0, 0.0, 0.0}));
    CHECK_GT(grounded_oracle_invalid({std::numeric_limits<double>::quiet_NaN(), 0.0, 0.0}));
    CHECK_GT(grounded_oracle_invalid({std::numeric_limits<double>::infinity(), 0.0, 0.0}));
}

void test_geometry_gates() {
    CHECK_GT(grounded_displacement_agrees({0.0, 0.0, 0.0}, {50.0, 0.0, 0.0}, {0.0, 0.0, 0.0}, {51.0, 0.0, 0.0}));
    CHECK_GT(!grounded_displacement_agrees({0.0, 0.0, 0.0}, {50.0, 0.0, 0.0}, {0.0, 0.0, 0.0}, {52.0, 0.0, 0.0}));
    CHECK_GT(grounded_displacement_agrees({0.0, 0.0, 0.0}, {10.0, 0.0, 0.0}, {0.0, 0.0, 0.0}, {10.25, 0.0, 0.0}));
    CHECK_GT(!grounded_displacement_agrees({0.0, 0.0, 0.0}, {10.0, 0.0, 0.0}, {0.0, 0.0, 0.0}, {10.26, 0.0, 0.0}));
    CHECK_GT(grounded_displacement_agrees(kA, kC, kA, kC));
    CHECK_GT(!grounded_displacement_agrees(kA, kC, kA, {kA.x + 50.0, kA.y, kA.z}));
    CHECK_GT(kGroundedDirectionCosine == 0.95);

    CHECK_GT(grounded_turn_stable(kE, kE));
    CHECK_GT(grounded_turn_stable(kE, shifted(kE, 0.25, 0.0, 0.0)));
    CHECK_GT(!grounded_turn_stable(kE, shifted(kE, 0.26, 0.0, 0.0)));
    CHECK_GT(grounded_double_read_stable(kA, shifted(kA, 0.05, 0.0, 0.0)));
    CHECK_GT(!grounded_double_read_stable(kA, shifted(kA, 1.0, 0.0, 0.0)));

    const GroundedVec3 older = kA;
    CHECK_GT(grounded_is_stale(kA, kC, &older, 1, {}));
    CHECK_GT(!grounded_is_stale(kC, kC, &older, 1, {}));
    CHECK_GT(grounded_standing_positions_separated(kA, kC, kE, kH));
    CHECK_GT(!grounded_standing_positions_separated(kA, shifted(kA, 10.0, 0.0, 0.0), kE, kH));
}

void test_region_and_copy() {
    GroundedRegion readable{};
    readable.base = 0x1000;
    readable.size = 4096;
    readable.state = kCommit;
    readable.protect = kReadWrite;
    readable.type = kPrivate;
    CHECK_GT(grounded_region_eligible(readable));
    readable.type = kImage;
    CHECK_GT(grounded_region_eligible(readable));
    readable.type = kMapped;
    CHECK_GT(!grounded_region_eligible(readable));
    readable.type = kPrivate;
    readable.protect = kReadWrite | kGuard;
    CHECK_GT(!grounded_region_eligible(readable));
    readable.protect = kNoAccess;
    CHECK_GT(!grounded_region_eligible(readable));
    readable.protect = kExecuteRead;
    CHECK_GT(!grounded_region_eligible(readable));

    alignas(8) std::uint8_t lo[4096]{};
    alignas(8) std::uint8_t hi[4096]{};
    const float x = 11.0f;
    const float y = 22.0f;
    const float z = 33.0f;
    std::memcpy(lo + 4088, &x, 4);
    std::memcpy(lo + 4092, &y, 4);
    std::memcpy(hi, &z, 4);
    GroundedRegion regions[2]{};
    regions[0].base = 0x5000;
    regions[0].size = 4096;
    regions[0].state = kCommit;
    regions[0].protect = kReadWrite;
    regions[0].type = kPrivate;
    regions[0].bytes = lo;
    regions[1] = regions[0];
    regions[1].base = 0x6000;
    regions[1].bytes = hi;
    GroundedView view{};
    view.regions = regions;
    view.region_count = 2;
    GroundedVec3 copied{};
    CHECK_GT(grounded_copy_triple(view, 0x5000 + 4088, copied));
    CHECK_GT(copied.x == 11.0 && copied.y == 22.0 && copied.z == 33.0);
    float swapped_y = 99.0f;
    std::memcpy(lo + 64, &y, 4);
    std::memcpy(lo + 68, &x, 4);
    std::memcpy(lo + 72, &swapped_y, 4);
    CHECK_GT(grounded_copy_triple(view, 0x5000 + 64, copied));
    GroundedVec3 offset{};
    CHECK_GT(!grounded_select_offset(copied, {11.0, 22.0, 33.0}, offset));

    regions[1].protect = kNoAccess;
    regions[1].bytes = nullptr;
    CHECK_GT(!grounded_copy_triple(view, 0x5000 + 4088, copied));
    regions[0].protect = kReadWrite | kGuard;
    CHECK_GT(!grounded_copy_triple(view, 0x5000 + 64, copied));
}

void test_watcher_and_time() {
    CHECK_GT(kGroundedWatcherPollMs == 50u);
    CHECK_GT(kGroundedOraclePeriodMs == 200u);
    CHECK_GT(kGroundedStandingWindowMs == 500u);
    CHECK_GT(kGroundedFullScanMs == 360000u);
    CHECK_GT(kGroundedRecheckMs == 15000u);

    const std::uint64_t before = grounded_observation_tick();
    OracleLogWatcher watcher;
    watcher.start_after_existing(0);
    watcher.poll_observed("DEFLUX14,7,A_STAND,1.500000,2.250000,3.125000\n");
    const std::uint64_t after = grounded_observation_tick();
    CHECK_GT(watcher.accepted() == 1u);
    CHECK_GT(watcher.samples().size() == 1u);
    CHECK_GT(watcher.samples()[0].sample_id == 7u);
    CHECK_GT(watcher.samples()[0].phase == "A_STAND");
    CHECK_GT(watcher.samples()[0].observed_ms >= before);
    CHECK_GT(watcher.samples()[0].observed_ms <= after);
    CHECK_GT(watcher.samples()[0].x == 1.5);
    CHECK_GT(watcher.samples()[0].y == 2.25);
    CHECK_GT(watcher.samples()[0].z == 3.125);

    const char* malformed[] = {
        "DEFLUX14,abc,A_STAND,1,2,3\n",
        "DEFLUX14,1,NO_SUCH,1,2,3\n",
        "DEFLUX14,1,A_STAND,nan,2,3\n",
        "DEFLUX14,1,A_STAND,1e2,2,3\n",
        "DEFLUX14,1,A_STAND,100001,0,0\n",
        "not a record\n",
    };
    for (const char* line : malformed) {
        OracleLogWatcher strict;
        strict.start_after_existing(0);
        strict.poll(line, 10);
        CHECK_GT(strict.accepted() == 0u);
        CHECK_GT(strict.rejected() == 1u);
        CHECK_GT(strict.samples().empty());
    }

    OracleLogWatcher partial;
    partial.start_after_existing(0);
    partial.poll("DEFLUX14,4,C_STAND,8.000000,9.000000,", 20);
    CHECK_GT(partial.accepted() == 0u);
    partial.poll("DEFLUX14,4,C_STAND,8.000000,9.000000,10.000000\n", 30);
    CHECK_GT(partial.accepted() == 1u);
    CHECK_GT(partial.samples()[0].z == 10.0);
    CHECK_GT(partial.samples()[0].observed_ms == 30u);

    const std::string old_line = "DEFLUX14,1,A_STAND,1.000000,1.000000,1.000000\n";
    const std::string both = old_line + "DEFLUX14,2,B_WALK,2.000000,2.000000,2.000000\n";
    OracleLogWatcher resumed;
    resumed.start_after_existing(old_line.size());
    resumed.poll(old_line, 1);
    CHECK_GT(resumed.accepted() == 0u);
    resumed.poll(both, 2);
    CHECK_GT(resumed.accepted() == 1u);
    CHECK_GT(resumed.samples()[0].sample_id == 2u);

    OracleLogWatcher truncated;
    truncated.start_after_existing(0);
    const std::string long_log = old_line + "DEFLUX14,2,B_WALK,2.000000,2.000000,2.000000\n";
    truncated.poll(long_log, 5);
    CHECK_GT(truncated.generation() == 0u);
    CHECK_GT(truncated.samples().size() == 2u);
    truncated.poll("DEFLUX14,9,H_STAND,4.000000,5.000000,6.000000\n", 6);
    CHECK_GT(truncated.generation() == 1u);
    CHECK_GT(truncated.samples().size() == 1u);
    CHECK_GT(truncated.samples()[0].sample_id == 9u);
    CHECK_GT(truncated.samples()[0].phase == "H_STAND");

    OracleSample series[4]{};
    series[0] = {1, "A_STAND", 0, 1.0, 0.0, 0.0};
    series[1] = {2, "A_STAND", 400, 100.0, 0.0, 0.0};
    series[2] = {3, "A_STAND", 1000, 2.0, 0.0, 0.0};
    series[3] = {4, "A_STAND", 1001, 9.0, 0.0, 0.0};
    OracleLogWatcher window;
    window.start_after_existing(0);
    std::string growing;
    for (const OracleSample& sample : series) {
        growing += "DEFLUX14," + std::to_string(sample.sample_id) + ",A_STAND,"
            + std::to_string(static_cast<int>(sample.x)) + ".000000,0.000000,0.000000\n";
        window.poll(growing, sample.observed_ms);
    }
    const std::vector<OracleSample> inside = window.samples_in_window(500, kGroundedStandingWindowMs);
    CHECK_GT(window.samples().size() == 4u);
    CHECK_GT(inside.size() == 3u);
    CHECK_GT(inside[0].sample_id == 1u);
    CHECK_GT(inside[2].sample_id == 3u);
    GroundedVec3 median{};
    OracleSample median_samples[3] = {
        {1, "A_STAND", 0, 1.0, 5.0, 7.0},
        {2, "A_STAND", 0, 100.0, 5.0, 7.0},
        {3, "A_STAND", 0, 2.0, 5.0, 7.0},
    };
    CHECK_GT(grounded_standing_median(median_samples, 3, median));
    CHECK_GT(median.x == 2.0 && median.y == 5.0 && median.z == 7.0);
    CHECK_GT(grounded_standing_oracle_stable(median_samples, 1));
    OracleSample unstable[2] = {
        {1, "A_STAND", 0, 0.0, 0.0, 0.0},
        {2, "A_STAND", 0, 1.0, 0.0, 0.0},
    };
    CHECK_GT(!grounded_standing_oracle_stable(unstable, 2));
}

void test_phase_identity_and_isolation() {
    CHECK_GT(grounded_phase_count() == 8u);
    CHECK_GT(std::strcmp(grounded_phase_name(0), "A_STAND") == 0);
    CHECK_GT(std::strcmp(grounded_phase_name(7), "H_STAND") == 0);
    CHECK_GT(grounded_phase_is_standing("A_STAND"));
    CHECK_GT(grounded_phase_is_standing("C_STAND"));
    CHECK_GT(grounded_phase_is_standing("E_STAND"));
    CHECK_GT(grounded_phase_is_standing("H_STAND"));
    CHECK_GT(!grounded_phase_is_standing("B_WALK"));
    CHECK_GT(!grounded_phase_is_standing("F_TURN"));
    CHECK_GT(grounded_phase_duration_ok(1000, 4000));
    CHECK_GT(!grounded_phase_duration_ok(1000, 3999));
    CHECK_GT(!grounded_phase_duration_ok(4000, 1000));
    CHECK_GT(grounded_vehicle_aborts_session(true));
    CHECK_GT(!grounded_vehicle_aborts_session(false));

    CHECK_GT(grounded_process_identity_holds(10, 99, "SanAndreas.exe", 10, 99, "SanAndreas.exe"));
    CHECK_GT(!grounded_process_identity_holds(10, 99, "SanAndreas.exe", 10, 100, "SanAndreas.exe"));
    CHECK_GT(!grounded_process_identity_holds(10, 99, "SanAndreas.exe", 11, 99, "SanAndreas.exe"));
    CHECK_GT(!grounded_process_identity_holds(0, 99, "SanAndreas.exe", 0, 99, "SanAndreas.exe"));

    const auto blocked = evaluate_grounded_live_attach();
    CHECK_GT(blocked.blocked);
    CHECK_GT(std::strcmp(blocked.message, "Live GTA resolution is isolated from automated tests.") == 0);
    _putenv_s("DEFLUX_RESEARCH_ISOLATE_LIVE_TARGET", "0");
    const auto unavailable = evaluate_grounded_live_attach();
    CHECK_GT(unavailable.blocked);
    CHECK_GT(std::strcmp(unavailable.message, "Live attach is not implemented for the grounded scanner.") == 0);
    _putenv_s("DEFLUX_RESEARCH_ISOLATE_LIVE_TARGET", "1");
    CHECK_GT(std::strcmp(grounded_session_claim(), kGroundedLiveClaimWithheld) == 0);
    CHECK_GT(!grounded_live_requirements_met(1, false));
    CHECK_GT(!grounded_live_requirements_met(0, true));
    CHECK_GT(grounded_live_requirements_met(1, true));
}

void test_scan_limits_and_markers() {
    const char ascii[] = "xxDEFLUX14yy";
    CHECK_GT(grounded_buffer_contains_marker(reinterpret_cast<const std::uint8_t*>(ascii), sizeof(ascii) - 1));
    std::uint8_t utf16[16]{};
    const char text[] = "DEFLUX14";
    for (int index = 0; index < 8; ++index) {
        utf16[index * 2] = static_cast<std::uint8_t>(text[index]);
    }
    CHECK_GT(grounded_buffer_contains_marker(utf16, sizeof(utf16)));
    const std::uint8_t empty[4] = {1, 2, 3, 4};
    CHECK_GT(!grounded_buffer_contains_marker(empty, sizeof(empty)));

    struct Tick {
        int calls = 0;
        std::uint64_t jump = 0;
    };
    Tick timeout{};
    timeout.jump = kGroundedFullScanMs + 1;
    const auto clock = [](void* context) -> std::uint64_t {
        auto* tick = static_cast<Tick*>(context);
        const int call = tick->calls++;
        return call == 0 ? 0 : tick->jump;
    };
    alignas(4) std::uint8_t page[64]{};
    const float x = 100.0f;
    const float y = 200.0f;
    const float z = 10.0f;
    std::memcpy(page, &x, 4);
    std::memcpy(page + 4, &y, 4);
    std::memcpy(page + 8, &z, 4);
    GroundedRegion region{};
    region.base = 0x8000;
    region.size = 64;
    region.state = kCommit;
    region.protect = kReadWrite;
    region.type = kPrivate;
    region.bytes = page;
    StreamScanRequest request{};
    request.view.regions = &region;
    request.view.region_count = 1;
    request.oracle = kA;
    request.clock.now_ms = clock;
    request.clock.context = &timeout;
    const auto timed_out = stream_scan_float32(request);
    CHECK_GT(timed_out.aborted);
    CHECK_GT(std::strcmp(timed_out.abort_reason, "timeout") == 0);
    CHECK_GT(timed_out.hits.empty());

    GroundedRegion huge = region;
    huge.base = 0x9000;
    huge.size = kGroundedMaxBytesConsidered + 1;
    huge.bytes = nullptr;
    request.view.regions = &huge;
    request.clock.now_ms = nullptr;
    const auto over_budget = stream_scan_float32(request);
    CHECK_GT(over_budget.aborted);
    CHECK_GT(std::strcmp(over_budget.abort_reason, "byte_budget") == 0);
    CHECK_GT(over_budget.hits.empty());

    std::vector<std::uint8_t> cap_bytes(static_cast<std::size_t>(kGroundedMaxCandidates + 1) * 16, 0);
    for (std::uint32_t index = 0; index < kGroundedMaxCandidates + 1; ++index) {
        std::memcpy(cap_bytes.data() + static_cast<std::size_t>(index) * 16, &x, 4);
        std::memcpy(cap_bytes.data() + static_cast<std::size_t>(index) * 16 + 4, &y, 4);
        std::memcpy(cap_bytes.data() + static_cast<std::size_t>(index) * 16 + 8, &z, 4);
    }
    GroundedRegion cap = region;
    cap.base = 0x30000000ull;
    cap.size = cap_bytes.size();
    cap.bytes = cap_bytes.data();
    request.view.regions = &cap;
    const auto capped = stream_scan_float32(request);
    CHECK_GT(capped.aborted);
    CHECK_GT(std::strcmp(capped.abort_reason, "candidate_cap") == 0);
    CHECK_GT(capped.hits.empty());
    CHECK_GT(capped.float64_hits.empty());

    GroundedHit hit{};
    hit.address = 0x8000;
    hit.x = 100;
    hit.y = 200;
    hit.z = 10;
    Tick recheck_tick{};
    recheck_tick.jump = 15001;
    RecheckRequest recheck{};
    recheck.hits = &hit;
    recheck.hit_count = 1;
    recheck.view.regions = &region;
    recheck.view.region_count = 1;
    recheck.oracle = kA;
    recheck.clock.now_ms = clock;
    recheck.clock.context = &recheck_tick;
    const auto recheck_timeout = recheck_grounded_hits(recheck);
    CHECK_GT(recheck_timeout.aborted);
    CHECK_GT(std::strcmp(recheck_timeout.abort_reason, "recheck_timeout") == 0);
    CHECK_GT(recheck_timeout.hits.empty());

    request.oracle = {std::numeric_limits<double>::quiet_NaN(), 0.0, 0.0};
    request.view.regions = &region;
    request.clock.now_ms = nullptr;
    const auto bad_oracle = stream_scan_float32(request);
    CHECK_GT(bad_oracle.aborted);
    CHECK_GT(std::strcmp(bad_oracle.abort_reason, "oracle_invalid") == 0);
}

void test_fixture_classification() {
    Fixture first = make_fixture();
    const GroundedQualification once = run_fixture(first);
    Fixture second = make_fixture();
    const GroundedQualification twice = run_fixture(second);
    CHECK_GT(!once.aborted);
    CHECK_GT(std::strcmp(once.published_live_claim, kGroundedLiveClaimWithheld) == 0);
    CHECK_GT(once.addresses.size() == twice.addresses.size());
    for (std::size_t index = 0; index < once.addresses.size(); ++index) {
        CHECK_GT(once.addresses[index].address == twice.addresses[index].address);
        CHECK_GT(once.addresses[index].evidence == twice.addresses[index].evidence);
    }

    const std::uint64_t true_address = first.address(PageTrue, 64);
    const std::uint64_t near_address = first.address(PageNear, 64);
    const std::uint64_t plus_address = first.address(PagePlus, 64);
    const std::uint64_t minus_address = first.address(PageMinus, 64);
    const std::uint64_t boundary_address = first.address(PageBoundaryLo, kPage - 8);
    CHECK_GT(evidence_is(once, true_address, GroundedEvidence::Strong));
    CHECK_GT(evidence_is(once, near_address, GroundedEvidence::Strong));
    CHECK_GT(evidence_is(once, plus_address, GroundedEvidence::Strong));
    CHECK_GT(evidence_is(once, minus_address, GroundedEvidence::Strong));
    CHECK_GT(evidence_is(once, boundary_address, GroundedEvidence::Strong));
    CHECK_GT(std::strcmp(to_string(GroundedEvidence::Strong), "GROUND_TRUTH_STRONG") == 0);

    std::vector<std::uint64_t> strong_addresses;
    for (const ClassifiedAddress& item : once.addresses) {
        if (item.evidence == GroundedEvidence::Strong) {
            strong_addresses.push_back(item.address);
        }
    }
    const std::uint64_t expected_strong[] = {
        true_address, near_address, plus_address, minus_address, boundary_address};
    CHECK_GT(strong_addresses.size() == 5u);
    for (std::uint64_t expected : expected_strong) {
        CHECK_GT(std::find(strong_addresses.begin(), strong_addresses.end(), expected) != strong_addresses.end());
    }

    CHECK_GT(find_address(once, first.address(PageArbitrary, 64)) == nullptr);
    CHECK_GT(find_address(once, first.address(PageInaccessibleLo, kPage - 8)) == nullptr);
    CHECK_GT(find_address(once, first.address(PageNan, 64)) == nullptr);
    CHECK_GT(find_address(once, first.address(PageInf, 64)) == nullptr);
    CHECK_GT(find_address(once, first.address(PageAbsurd, 64)) == nullptr);
    CHECK_GT(find_address(once, first.address(PageCleo, 64)) == nullptr);
    CHECK_GT(find_address(once, first.address(PagePlugin, 64)) == nullptr);
    CHECK_GT(find_address(once, first.address(PageResearchModule, 64)) == nullptr);
    CHECK_GT(find_address(once, first.address(PageResearchBuffer, 64)) == nullptr);
    CHECK_GT(find_address(once, first.address(PageGuard, 64)) == nullptr);

    CHECK_GT(evidence_is(once, first.address(PageStale, 64), GroundedEvidence::StaleCopy));
    CHECK_GT(evidence_is(once, first.address(PageGameImage, 128), GroundedEvidence::StaleCopy));
    CHECK_GT(evidence_is(once, first.address(PageCamera, 64), GroundedEvidence::Rejected));
    CHECK_GT(evidence_is(once, first.address(PagePhysics, 64), GroundedEvidence::Rejected));
    CHECK_GT(evidence_is(once, first.address(PageFailC, 64), GroundedEvidence::Rejected));
    CHECK_GT(evidence_is(once, first.address(PageMarker, 128), GroundedEvidence::CleoContamination));
    CHECK_GT(evidence_is(once, first.address(PageSilence, 64), GroundedEvidence::CleoContamination));
    const ClassifiedAddress* float64 = find_address(once, first.address(PageFloat64, 64));
    CHECK_GT(float64 != nullptr);
    CHECK_GT(float64->float64_only);
    CHECK_GT(float64->evidence == GroundedEvidence::CleoContamination);

    int dense_moderate = 0;
    for (int copy = 0; copy < 9; ++copy) {
        const ClassifiedAddress* dense = find_address(once, first.address(PageDense, 64 + static_cast<std::uint64_t>(copy) * 16));
        CHECK_GT(dense != nullptr);
        CHECK_GT(dense->evidence == GroundedEvidence::Moderate);
        CHECK_GT(std::strcmp(dense->reason, "dense_page_unresolved") == 0);
        ++dense_moderate;
    }
    CHECK_GT(dense_moderate == 9);

    GroundedQualifyRequest vehicle = {};
    vehicle.in_vehicle = true;
    vehicle.prepare = prepare_fixture;
    vehicle.context = &first;
    vehicle.oracle_a = kA;
    vehicle.oracle_c = kC;
    vehicle.oracle_e = kE;
    vehicle.oracle_h = kH;
    const GroundedQualification vehicle_result = qualify_grounded_session(vehicle);
    CHECK_GT(vehicle_result.aborted);
    CHECK_GT(std::strcmp(vehicle_result.abort_reason, "vehicle_semantic_change") == 0);
    CHECK_GT(std::strcmp(vehicle_result.published_live_claim, kGroundedLiveClaimWithheld) == 0);
    CHECK_GT(vehicle_result.addresses.empty());
}

void test_live_gate_and_isolation() {
    using deflux::game::gta_sa_de::GroundedLiveBlock;
    using deflux::game::gta_sa_de::GroundedLiveGateInput;
    using deflux::game::gta_sa_de::evaluate_grounded_live_gate;
    using deflux::game::gta_sa_de::grounded_asi_loader_sha_is_official;
    using deflux::game::gta_sa_de::grounded_live_access_is_read_only;
    using deflux::game::gta_sa_de::grounded_live_process_access;
    using deflux::game::gta_sa_de::grounded_live_result_token;
    using deflux::game::gta_sa_de::grounded_live_requirements_met;
    using deflux::game::gta_sa_de::kGroundedNoCandidate;
    using deflux::game::gta_sa_de::kOfficialAsiLoaderSha256;

    const std::uint32_t access = grounded_live_process_access();
    CHECK_GT(access == (0x1000u | 0x0010u));
    CHECK_GT((access & 0x0020u) == 0u);
    CHECK_GT((access & 0x0008u) == 0u);
    CHECK_GT((access & 0x0002u) == 0u);
    CHECK_GT(grounded_live_access_is_read_only(access));
    CHECK_GT(!grounded_live_access_is_read_only(access | 0x0020u));
    CHECK_GT(!grounded_live_access_is_read_only(access | 0x0008u));
    CHECK_GT(!grounded_live_access_is_read_only(access | 0x0002u));
    CHECK_GT(grounded_asi_loader_sha_is_official(kOfficialAsiLoaderSha256));
    CHECK_GT(!grounded_asi_loader_sha_is_official("0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef"));

    GroundedLiveGateInput gate{};
    CHECK_GT(evaluate_grounded_live_gate(gate) == GroundedLiveBlock::IntentMissing);
    gate.explicit_intent = true;
    gate.isolation_engaged = true;
    CHECK_GT(evaluate_grounded_live_gate(gate) == GroundedLiveBlock::IsolationEngaged);
    gate.isolation_engaged = false;
    gate.requested_access = access | 0x0020u;
    CHECK_GT(evaluate_grounded_live_gate(gate) == GroundedLiveBlock::WriteAccessRejected);
    gate.requested_access = access;
    CHECK_GT(evaluate_grounded_live_gate(gate) == GroundedLiveBlock::BuildSizeMismatch);
    gate.size_matches = true;
    CHECK_GT(evaluate_grounded_live_gate(gate) == GroundedLiveBlock::BuildShaMismatch);
    gate.sha_matches = true;
    CHECK_GT(evaluate_grounded_live_gate(gate) == GroundedLiveBlock::BuildVersionMismatch);
    gate.version_matches = true;
    CHECK_GT(evaluate_grounded_live_gate(gate) == GroundedLiveBlock::TargetAbsent);
    gate.process_present = true;
    CHECK_GT(evaluate_grounded_live_gate(gate) == GroundedLiveBlock::ProcessPathMismatch);
    gate.path_matches = true;
    CHECK_GT(evaluate_grounded_live_gate(gate) == GroundedLiveBlock::CreationMissing);
    gate.creation_present = true;
    CHECK_GT(evaluate_grounded_live_gate(gate) == GroundedLiveBlock::ModuleIdentityMismatch);
    gate.module_identity_matches = true;
    CHECK_GT(evaluate_grounded_live_gate(gate) == GroundedLiveBlock::Ready);

    CHECK_GT(std::strcmp(grounded_live_result_token(true, 1, true), "FIRST_PROCESS_CANDIDATE_ONLY") == 0);
    CHECK_GT(std::strcmp(grounded_live_result_token(true, 0, true), kGroundedNoCandidate) == 0);
    CHECK_GT(std::strcmp(grounded_live_result_token(false, 1, false), kGroundedNoCandidate) == 0);
    CHECK_GT(std::strcmp(grounded_live_result_token(false, 0, true), kGroundedNoCandidate) == 0);
    CHECK_GT(grounded_live_requirements_met(1, true));
    const char* success = grounded_live_result_token(false, 1, true);
    CHECK_GT(std::strcmp(success, "FIRST_PROCESS_CANDIDATE_ONLY") != 0);
    CHECK_GT(std::strcmp(success, kGroundedNoCandidate) != 0);

    char arg0[] = "DeFlux_ResearchLauncher.exe";
    char arg1[] = "--grounded-live";
    char* argv[] = {arg0, arg1};
    const auto isolated = deflux::game::gta_sa_de::run_research_launcher(2, argv);
    CHECK_GT(!isolated.ok);
    CHECK_GT(!isolated.inject_requested);
    CHECK_GT(std::strcmp(isolated.message.c_str(), "Live GTA resolution is isolated from automated tests.") == 0);
}

void expect_envelope_rejected(const std::string& line) {
    OracleLogWatcher watcher;
    watcher.start_after_existing(0);
    watcher.poll(line, 10);
    CHECK_GT(watcher.accepted() == 0u);
    CHECK_GT(watcher.rejected() == 1u);
    CHECK_GT(watcher.samples().empty());
}

void test_cleo_log_envelope() {
    const char* observed =
        "17:22:58 [INFO] [\"deflux1\"] \"DEFLUX14,1,A_STAND,1933.431152,-2492.366699,13.546875\"\n";
    OracleLogWatcher live;
    live.start_after_existing(0);
    live.poll(observed, 50);
    CHECK_GT(live.accepted() == 1u);
    CHECK_GT(live.rejected() == 0u);
    CHECK_GT(live.samples().size() == 1u);
    CHECK_GT(live.samples()[0].sample_id == 1u);
    CHECK_GT(live.samples()[0].phase == "A_STAND");
    CHECK_GT(live.samples()[0].x == 1933.431152);
    CHECK_GT(live.samples()[0].y == -2492.366699);
    CHECK_GT(live.samples()[0].z == 13.546875);
    CHECK_GT(live.samples()[0].observed_ms == 50u);

    const char* other_name =
        "17:23:00 [INFO] [\"street_oracle\"] \"DEFLUX14,8,A_STAND,1933.431152,-2492.366699,13.546875\"\n";
    OracleLogWatcher renamed;
    renamed.start_after_existing(0);
    renamed.poll(other_name, 60);
    CHECK_GT(renamed.accepted() == 1u);
    CHECK_GT(renamed.samples()[0].sample_id == 8u);
    CHECK_GT(renamed.samples()[0].phase == "A_STAND");
    CHECK_GT(renamed.samples()[0].x == 1933.431152);
    CHECK_GT(renamed.samples()[0].y == -2492.366699);
    CHECK_GT(renamed.samples()[0].z == 13.546875);

    expect_envelope_rejected("17:22:58 [INFO] [\"deflux1\"] \"Script loaded successfully\"\n");
    expect_envelope_rejected(
        "17:22:58 [ERROR] [\"deflux1\"] \"DEFLUX14,1,A_STAND,1.000000,2.000000,3.000000\"\n");
    expect_envelope_rejected(
        "17:22:58 [INFO] [\"deflux1\"] DEFLUX14,1,A_STAND,1.000000,2.000000,3.000000\n");
    expect_envelope_rejected(
        "17:22:58 [INFO] [\"deflux1\"] \"DEFLUX14,1,A_STAND,1.000000,2.000000,3.000000\n");
    expect_envelope_rejected(
        "17:22:58 [INFO] [\"deflux1\"] \"prefixDEFLUX14,1,A_STAND,1.000000,2.000000,3.000000\"\n");
    expect_envelope_rejected(
        "17:22:58 [INFO] [\"deflux1\"] \"DEFLUX14,1,A_STAND,1.000000,2.000000,3.000000 extra\"\n");
    expect_envelope_rejected(
        "17:22:58 [INFO] [\"deflux1\"] \"DEFLUX14,1,A_STAND,1.000000,2.000000,3.000000\" trailing\n");
    expect_envelope_rejected(
        "17:22:58 [INFO] [\"deflux1\"] \"DEFLUX14,abc,A_STAND,1.000000,2.000000,3.000000\"\n");
    expect_envelope_rejected(
        "17:22:58 [INFO] [\"deflux1\"] \"DEFLUX14,1,NO_SUCH,1.000000,2.000000,3.000000\"\n");
    expect_envelope_rejected(
        "17:22:58 [INFO] [\"deflux1\"] \"DEFLUX14,1,A_STAND,1.2.3,2.000000,3.000000\"\n");
    const std::string overflow_coordinate(400, '9');
    expect_envelope_rejected(
        "17:22:58 [INFO] [\"deflux1\"] \"DEFLUX14,1,A_STAND," + overflow_coordinate
        + ",0.000000,0.000000\"\n");
    expect_envelope_rejected("prefixDEFLUX14,1,A_STAND,1.000000,2.000000,3.000000\n");
    expect_envelope_rejected("DEFLUX14junk,1,A_STAND,1.000000,2.000000,3.000000\n");
    expect_envelope_rejected("arbitrary text DEFLUX14,1,A_STAND,1.000000,2.000000,3.000000\n");

    const std::string partial_physical =
        "17:22:58 [INFO] [\"deflux1\"] \"DEFLUX14,1,A_STAND,1933.431152,-2492.366699,";
    OracleLogWatcher partial;
    partial.start_after_existing(0);
    partial.poll(partial_physical, 70);
    CHECK_GT(partial.accepted() == 0u);
    CHECK_GT(partial.rejected() == 0u);
    CHECK_GT(partial.samples().empty());
    partial.poll(std::string(observed), 80);
    CHECK_GT(partial.accepted() == 1u);
    CHECK_GT(partial.samples()[0].sample_id == 1u);
    CHECK_GT(partial.samples()[0].z == 13.546875);
    CHECK_GT(partial.samples()[0].observed_ms == 80u);

    const std::string old_envelope =
        "17:22:57 [INFO] [\"deflux1\"] \"DEFLUX14,1,A_STAND,1.000000,2.000000,3.000000\"\n";
    const std::string resumed_log = old_envelope + observed;
    OracleLogWatcher resumed;
    resumed.start_after_existing(old_envelope.size());
    resumed.poll(old_envelope, 1);
    CHECK_GT(resumed.accepted() == 0u);
    resumed.poll(resumed_log, 2);
    CHECK_GT(resumed.accepted() == 1u);
    CHECK_GT(resumed.samples()[0].sample_id == 1u);
    CHECK_GT(resumed.samples()[0].x == 1933.431152);

    OracleLogWatcher truncated;
    truncated.start_after_existing(0);
    truncated.poll(resumed_log, 5);
    CHECK_GT(truncated.generation() == 0u);
    CHECK_GT(truncated.samples().size() == 2u);
    truncated.poll(observed, 6);
    CHECK_GT(truncated.generation() == 1u);
    CHECK_GT(truncated.samples().size() == 1u);
    CHECK_GT(truncated.samples()[0].y == -2492.366699);

    const std::filesystem::path replay_path =
        std::filesystem::temp_directory_path() / "deflux14c1_cleo_redux.log";
    {
        std::ofstream replay(replay_path, std::ios::binary | std::ios::trunc);
        replay << "17:22:50 [INFO] CLEO Redux 1.5.0 started\n";
        replay << observed;
        replay << other_name;
    }
    std::ifstream input(replay_path, std::ios::binary);
    const std::string replay_text((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
    input.close();
    std::filesystem::remove(replay_path);
    OracleLogWatcher replay_watcher;
    replay_watcher.start_after_existing(0);
    replay_watcher.poll(replay_text, 90);
    CHECK_GT(replay_watcher.accepted() > 0u);
    CHECK_GT(replay_watcher.samples().size() == 2u);
    CHECK_GT(replay_watcher.samples()[0].sample_id == 1u);
    CHECK_GT(replay_watcher.samples()[0].phase == "A_STAND");
    CHECK_GT(replay_watcher.samples()[0].x == 1933.431152);
    CHECK_GT(replay_watcher.samples()[0].y == -2492.366699);
    CHECK_GT(replay_watcher.samples()[0].z == 13.546875);
    CHECK_GT(replay_watcher.rejected() == 1u);
}

struct SharedLogWriter {
    HANDLE handle = INVALID_HANDLE_VALUE;

    explicit SharedLogWriter(const std::wstring& path) {
        handle = CreateFileW(path.c_str(),
                              GENERIC_WRITE,
                              FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                              nullptr,
                              CREATE_ALWAYS,
                              FILE_ATTRIBUTE_NORMAL,
                              nullptr);
    }

    ~SharedLogWriter() {
        if (handle != INVALID_HANDLE_VALUE) {
            CloseHandle(handle);
        }
    }

    bool append(std::string_view text) const {
        DWORD wrote = 0;
        if (handle == INVALID_HANDLE_VALUE
            || !WriteFile(handle, text.data(), static_cast<DWORD>(text.size()), &wrote, nullptr)
            || wrote != text.size()) {
            return false;
        }
        return FlushFileBuffers(handle) != FALSE;
    }

    std::uint64_t size() const {
        LARGE_INTEGER value{};
        if (handle == INVALID_HANDLE_VALUE || !GetFileSizeEx(handle, &value) || value.QuadPart < 0) {
            return 0;
        }
        return static_cast<std::uint64_t>(value.QuadPart);
    }

    bool replace_with(std::string_view text) const {
        if (handle == INVALID_HANDLE_VALUE || SetFilePointer(handle, 0, nullptr, FILE_BEGIN) == INVALID_SET_FILE_POINTER) {
            return false;
        }
        if (!SetEndOfFile(handle)) {
            return false;
        }
        return append(text);
    }
};

void test_live_log_append_after_eof() {
    const std::filesystem::path path = std::filesystem::temp_directory_path() / L"deflux14c2_cleo_redux.log";
    const std::wstring wide = path.wstring();
    std::filesystem::remove(path);
    SharedLogWriter writer(wide);
    CHECK_GT(writer.handle != INVALID_HANDLE_VALUE);
    const std::string historical =
        "17:38:00 [INFO] CLEO Redux started\n"
        "17:38:01 [INFO] [\"deflux1\"] \"DEFLUX14,99,H_STAND,1.000000,2.000000,3.000000\"\n";
    CHECK_GT(writer.append(historical));

    OracleLogWatcher watcher;
    watcher.start_after_existing(writer.size());
    CHECK_GT(watcher.poll_file(wide, 1));
    CHECK_GT(watcher.poll_file(wide, 2));
    CHECK_GT(watcher.poll_file(wide, 3));
    CHECK_GT(watcher.accepted() == 0u);
    CHECK_GT(watcher.samples().empty());

    const char* first =
        "17:39:01 [INFO] [\"deflux1\"] \"DEFLUX14,1,A_STAND,1955.317871,-2464.069580,13.546875\"\n";
    CHECK_GT(writer.append(first));
    CHECK_GT(watcher.poll_file(wide, 10));
    CHECK_GT(watcher.accepted() == 1u);
    CHECK_GT(watcher.samples().size() == 1u);
    CHECK_GT(watcher.samples()[0].sample_id == 1u);
    CHECK_GT(watcher.samples()[0].phase == "A_STAND");
    CHECK_GT(watcher.samples()[0].x == 1955.317871);
    CHECK_GT(watcher.samples()[0].y == -2464.069580);
    CHECK_GT(watcher.samples()[0].z == 13.546875);

    const char* second =
        "17:39:02 [INFO] [\"deflux1\"] \"DEFLUX14,2,A_STAND,1955.317871,-2464.069580,13.546875\"\n";
    CHECK_GT(writer.append(second));
    CHECK_GT(watcher.poll_file(wide, 20));
    CHECK_GT(watcher.accepted() == 2u);
    CHECK_GT(watcher.samples()[1].sample_id == 2u);
    CHECK_GT(watcher.samples()[1].z == 13.546875);

    const std::string batch =
        std::string("17:39:03 [INFO] [\"deflux1\"] \"DEFLUX14,3,B_WALK,1956.000000,-2464.069580,13.546875\"\n")
        + "17:39:03 [INFO] [\"deflux1\"] \"DEFLUX14,4,B_WALK,1957.000000,-2464.069580,13.546875\"\n";
    CHECK_GT(writer.append(batch));
    CHECK_GT(watcher.poll_file(wide, 30));
    CHECK_GT(watcher.accepted() == 4u);
    CHECK_GT(watcher.samples()[2].sample_id == 3u);
    CHECK_GT(watcher.samples()[3].sample_id == 4u);

    const std::string split_left =
        "17:39:04 [INFO] [\"deflux1\"] \"DEFLUX14,5,A_STAND,1955.317871,-2464.069580,";
    CHECK_GT(writer.append(split_left));
    CHECK_GT(watcher.poll_file(wide, 40));
    CHECK_GT(watcher.accepted() == 4u);
    CHECK_GT(writer.append("13.546875\"\n"));
    CHECK_GT(watcher.poll_file(wide, 50));
    CHECK_GT(watcher.accepted() == 5u);
    CHECK_GT(watcher.samples().back().sample_id == 5u);
    CHECK_GT(watcher.samples().back().x == 1955.317871);

    const std::string mixed =
        "17:39:05 [INFO] [\"deflux1\"] \"still armed\"\n"
        "17:39:05 [INFO] [\"deflux1\"] \"DEFLUX14,6,A_STAND,1955.317871,-2464.069580,13.546875\"\n"
        "17:39:05 [INFO] [\"deflux1\"] \"DEFLUX14,zz,A_STAND,1.000000,2.000000,3.000000\"\n";
    const std::uint32_t rejected_before = watcher.rejected();
    CHECK_GT(writer.append(mixed));
    CHECK_GT(watcher.poll_file(wide, 60));
    CHECK_GT(watcher.accepted() == 6u);
    CHECK_GT(watcher.rejected() == rejected_before + 2u);
    CHECK_GT(watcher.samples().back().sample_id == 6u);

    CHECK_GT(watcher.poll_file(wide, 70));
    CHECK_GT(watcher.poll_file(wide, 71));
    CHECK_GT(watcher.accepted() == 6u);
    CHECK_GT(watcher.samples().size() == 6u);

    const std::string recreated =
        "17:40:00 [INFO] [\"street_oracle\"] \"DEFLUX14,7,C_STAND,4.000000,5.000000,6.000000\"\n";
    CHECK_GT(watcher.generation() == 0u);
    CHECK_GT(writer.replace_with(recreated));
    CHECK_GT(watcher.poll_file(wide, 80));
    CHECK_GT(watcher.generation() == 1u);
    CHECK_GT(watcher.accepted() == 1u);
    CHECK_GT(watcher.samples().size() == 1u);
    CHECK_GT(watcher.samples()[0].sample_id == 7u);
    CHECK_GT(watcher.samples()[0].phase == "C_STAND");
    CloseHandle(writer.handle);
    writer.handle = INVALID_HANDLE_VALUE;
    std::filesystem::remove(path);
}

struct ScriptClock {
    int calls = 0;
    int expire_at = 1000000;
    std::uint64_t origin = 0;
    std::uint64_t step = 1;
    std::uint64_t jump = 0;
};

std::uint64_t script_clock_now(void* context) {
    auto* clock = static_cast<ScriptClock*>(context);
    const int call = clock->calls++;
    if (call >= clock->expire_at) {
        return clock->origin + clock->jump;
    }
    return clock->origin + static_cast<std::uint64_t>(call) * clock->step;
}

GroundedRegion private_region(std::uint64_t base, std::vector<std::uint8_t>& bytes) {
    GroundedRegion region{};
    region.base = base;
    region.size = bytes.size();
    region.state = kCommit;
    region.protect = kReadWrite;
    region.type = kPrivate;
    region.bytes = bytes.data();
    return region;
}

void plant_f32(std::vector<std::uint8_t>& bytes, std::size_t offset, float x, float y, float z) {
    std::memcpy(bytes.data() + offset, &x, 4);
    std::memcpy(bytes.data() + offset + 4, &y, 4);
    std::memcpy(bytes.data() + offset + 8, &z, 4);
}

StreamScanResult scan_region(GroundedRegion& region, ScriptClock& clock, std::uint32_t time_limit_ms) {
    StreamScanRequest request{};
    request.view.regions = &region;
    request.view.region_count = 1;
    request.oracle = kA;
    request.clock.now_ms = script_clock_now;
    request.clock.context = &clock;
    request.time_limit_ms = time_limit_ms;
    return stream_scan_float32(request);
}

void test_scan_deadlines() {
    using deflux::game::gta_sa_de::kGroundedNoCandidate;
    using deflux::game::gta_sa_de::kGroundedScanTimeSliceBytes;
    using deflux::game::gta_sa_de::kGroundedStageEnumeration;
    using deflux::game::gta_sa_de::kGroundedStageFilter;
    using deflux::game::gta_sa_de::kGroundedStageMatch;
    using deflux::game::gta_sa_de::kGroundedStageNone;
    using deflux::game::gta_sa_de::kGroundedStageRead;
    using deflux::game::gta_sa_de::grounded_live_result_token;

    std::vector<std::uint8_t> small(256, 0);
    plant_f32(small, 0, 100.0f, 200.0f, 10.0f);
    plant_f32(small, 32, 100.0f, 200.0f, 50.0f);
    GroundedRegion small_region = private_region(0x80000000ull, small);

    ScriptClock waited{};
    waited.origin = 92000;
    waited.step = 10;
    const StreamScanResult after_wait = scan_region(small_region, waited, kGroundedFullScanMs);
    CHECK_GT(!after_wait.aborted);
    CHECK_GT(!after_wait.timing.partial_scan);
    CHECK_GT(std::strcmp(after_wait.timing.timeout_stage, kGroundedStageNone) == 0);
    CHECK_GT(after_wait.timing.timeout_budget_ms == kGroundedFullScanMs);
    CHECK_GT(after_wait.timing.total_scan_ms < 1000u);
    CHECK_GT(after_wait.hits.size() == 1u);
    CHECK_GT(after_wait.hits[0].address == 0x80000000ull);
    CHECK_GT(after_wait.hits[0].x == 100.0);
    CHECK_GT(after_wait.hits[0].y == 200.0);
    CHECK_GT(after_wait.hits[0].z == 10.0);

    std::vector<std::uint64_t> expected;
    for (std::size_t offset = 0; offset + 12 <= small.size(); offset += 4) {
        float x = 0.0f;
        float y = 0.0f;
        float z = 0.0f;
        std::memcpy(&x, small.data() + offset, 4);
        std::memcpy(&y, small.data() + offset + 4, 4);
        std::memcpy(&z, small.data() + offset + 8, 4);
        const GroundedVec3 value{static_cast<double>(x), static_cast<double>(y), static_cast<double>(z)};
        GroundedVec3 offset_out{};
        if (grounded_select_offset(value, kA, offset_out)) {
            expected.push_back(0x80000000ull + offset);
        }
    }
    CHECK_GT(expected.size() == after_wait.hits.size());
    for (std::size_t index = 0; index < expected.size(); ++index) {
        CHECK_GT(after_wait.hits[index].address == expected[index]);
    }

    std::vector<std::uint8_t> wide(static_cast<std::size_t>(kGroundedScanTimeSliceBytes + 64), 0);
    plant_f32(wide, 0, 100.0f, 200.0f, 10.0f);
    GroundedRegion wide_region = private_region(0x90000000ull, wide);
    ScriptClock match_clock{};
    match_clock.expire_at = 3;
    match_clock.jump = static_cast<std::uint64_t>(kGroundedFullScanMs) + 1ull;
    const StreamScanResult timed_out = scan_region(wide_region, match_clock, kGroundedFullScanMs);
    CHECK_GT(timed_out.aborted);
    CHECK_GT(std::strcmp(timed_out.abort_reason, "timeout") == 0);
    CHECK_GT(timed_out.timing.partial_scan);
    CHECK_GT(timed_out.hits.empty());
    CHECK_GT(timed_out.float64_hits.empty());
    CHECK_GT(timed_out.timing.bytes_read > 0u);
    CHECK_GT(std::strcmp(timed_out.timing.timeout_stage, kGroundedStageMatch) == 0);

    ScriptClock enumeration_clock{};
    enumeration_clock.expire_at = 1;
    enumeration_clock.jump = static_cast<std::uint64_t>(kGroundedFullScanMs) + 1ull;
    const StreamScanResult enumeration = scan_region(small_region, enumeration_clock, kGroundedFullScanMs);
    CHECK_GT(enumeration.aborted);
    CHECK_GT(enumeration.hits.empty());
    CHECK_GT(!enumeration.timing.partial_scan);
    CHECK_GT(std::strcmp(enumeration.timing.timeout_stage, kGroundedStageEnumeration) == 0);

    ScriptClock read_clock{};
    read_clock.expire_at = 2;
    read_clock.jump = static_cast<std::uint64_t>(kGroundedFullScanMs) + 1ull;
    const StreamScanResult read_timeout = scan_region(small_region, read_clock, kGroundedFullScanMs);
    CHECK_GT(read_timeout.aborted);
    CHECK_GT(read_timeout.hits.empty());
    CHECK_GT(read_timeout.timing.partial_scan);
    CHECK_GT(read_timeout.timing.bytes_attempted > 0u);
    CHECK_GT(std::strcmp(read_timeout.timing.timeout_stage, kGroundedStageRead) == 0);

    ScriptClock filter_clock{};
    filter_clock.expire_at = 3;
    filter_clock.jump = static_cast<std::uint64_t>(kGroundedFullScanMs) + 1ull;
    const StreamScanResult filter_timeout = scan_region(small_region, filter_clock, kGroundedFullScanMs);
    CHECK_GT(filter_timeout.aborted);
    CHECK_GT(filter_timeout.hits.empty());
    CHECK_GT(filter_timeout.timing.partial_scan);
    CHECK_GT(std::strcmp(filter_timeout.timing.timeout_stage, kGroundedStageFilter) == 0);

    ScriptClock fresh{};
    fresh.step = 1;
    const StreamScanResult completed = scan_region(small_region, fresh, kGroundedFullScanMs);
    CHECK_GT(!completed.aborted);
    CHECK_GT(!completed.timing.partial_scan);
    CHECK_GT(std::strcmp(completed.timing.timeout_stage, kGroundedStageNone) == 0);
    CHECK_GT(completed.hits.size() == 1u);

    GroundedQualifyRequest rejected{};
    rejected.oracle_a = kA;
    rejected.oracle_c = kC;
    rejected.oracle_e = kE;
    rejected.oracle_h = kH;
    rejected.view.regions = &wide_region;
    rejected.view.region_count = 1;
    ScriptClock qualify_clock{};
    qualify_clock.expire_at = 3;
    qualify_clock.jump = static_cast<std::uint64_t>(kGroundedFullScanMs) + 1ull;
    rejected.clock.now_ms = script_clock_now;
    rejected.clock.context = &qualify_clock;
    rejected.prepare = [](void*, std::uint32_t) {};
    const GroundedQualification qualification = qualify_grounded_session(rejected);
    CHECK_GT(qualification.aborted);
    CHECK_GT(std::strcmp(qualification.abort_reason, "timeout") == 0);
    CHECK_GT(qualification.addresses.empty());
    CHECK_GT(std::strcmp(qualification.published_live_claim, kGroundedLiveClaimWithheld) == 0);
    CHECK_GT(std::strcmp(grounded_live_result_token(true, 0, false), kGroundedNoCandidate) == 0);
    CHECK_GT(std::strcmp(grounded_live_result_token(true, 0, false), "TRANSFORM_CANDIDATE_ESTABLISHED") != 0);

    constexpr std::size_t kBenchBytes = 4ull * 1024ull * 1024ull;
    std::vector<std::uint8_t> bench(kBenchBytes, 0);
    GroundedRegion bench_region = private_region(0x60000000ull, bench);
    StreamScanRequest bench_request{};
    bench_request.view.regions = &bench_region;
    bench_request.view.region_count = 1;
    bench_request.oracle = kA;
    bench_request.time_limit_ms = kGroundedFullScanMs;
    const StreamScanResult measured = stream_scan_float32(bench_request);
    std::printf("  scan bench bytes=%zu total_scan_ms=%llu match_ms=%llu read_ms=%llu filter_ms=%llu aborted=%d\n",
                bench.size(),
                static_cast<unsigned long long>(measured.timing.total_scan_ms),
                static_cast<unsigned long long>(measured.timing.match_ms),
                static_cast<unsigned long long>(measured.timing.read_ms),
                static_cast<unsigned long long>(measured.timing.filter_ms),
                measured.aborted ? 1 : 0);
    CHECK_GT(!measured.aborted);
    CHECK_GT(!measured.timing.partial_scan);
    CHECK_GT(measured.hits.empty());
    CHECK_GT(std::strcmp(measured.timing.timeout_stage, kGroundedStageNone) == 0);
}

} // namespace

int run_qualification_grounded_transform_tests(int& passed, int& failed) {
    test_match_boundaries();
    test_geometry_gates();
    test_region_and_copy();
    test_watcher_and_time();
    test_phase_identity_and_isolation();
    test_scan_limits_and_markers();
    test_fixture_classification();
    test_live_gate_and_isolation();
    test_cleo_log_envelope();
    test_live_log_append_after_eof();
    test_scan_deadlines();
    passed += g_passed;
    failed += g_failed;
    return g_failed == 0 ? 0 : 1;
}

} // namespace deflux_tests
