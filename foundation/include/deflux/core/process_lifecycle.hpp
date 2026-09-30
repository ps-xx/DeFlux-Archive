#pragma once

#include <cstdint>
#include <string>
#include <string_view>

namespace deflux::core {

// OS process observation for a game adapter. Independent of SessionState
// (DISCONNECTED..PLAYING), compatibility, attachment, and engine/world/player
// readiness. PROCESS_EXITED is a one-shot transition, not a sticky state.
enum class ProcessLifecycle : std::uint8_t {
    NotRunning = 0,
    ProcessDetected,
    ProcessExited,
    ObservationError,
};

// Adapter-supplied identity. creation_identity is an opaque OS creation stamp
// (not a Unix time). PID alone is not unique because operating systems reuse PIDs.
struct ProcessInstance {
    std::uint32_t process_id = 0;
    std::uint64_t creation_identity = 0;
    std::string executable_path;
};

struct ProcessObservation {
    ProcessLifecycle state = ProcessLifecycle::NotRunning;
    ProcessInstance instance{};
    bool instance_changed = false;
    std::string message;
};

enum class ProcessSampleKind : std::uint8_t {
    Absent = 0,
    Present,
    Error,
};

struct ProcessSample {
    ProcessSampleKind kind = ProcessSampleKind::Absent;
    ProcessInstance instance{};
    std::string message;
};

const char* to_string(ProcessLifecycle state) noexcept;
const char* to_string(ProcessSampleKind kind) noexcept;

bool is_valid_process_instance(const ProcessInstance& instance) noexcept;
bool same_process_instance(const ProcessInstance& left, const ProcessInstance& right) noexcept;

ProcessSample process_sample_absent(std::string message);
ProcessSample process_sample_present(ProcessInstance instance, std::string message);
ProcessSample process_sample_error(std::string message);

// Pure transition. previous.state == ProcessExited is never kept across a second poll.
ProcessObservation advance_process_lifecycle(const ProcessObservation& previous,
                                             const ProcessSample& sample);

} // namespace deflux::core
