#include "deflux/core/process_lifecycle.hpp"

namespace deflux::core {

const char* to_string(ProcessLifecycle state) noexcept {
    switch (state) {
    case ProcessLifecycle::NotRunning:
        return "NOT_RUNNING";
    case ProcessLifecycle::ProcessDetected:
        return "PROCESS_DETECTED";
    case ProcessLifecycle::ProcessExited:
        return "PROCESS_EXITED";
    case ProcessLifecycle::ObservationError:
        return "OBSERVATION_ERROR";
    }
    return "UNKNOWN";
}

const char* to_string(ProcessSampleKind kind) noexcept {
    switch (kind) {
    case ProcessSampleKind::Absent:
        return "ABSENT";
    case ProcessSampleKind::Present:
        return "PRESENT";
    case ProcessSampleKind::Error:
        return "ERROR";
    }
    return "UNKNOWN";
}

bool is_valid_process_instance(const ProcessInstance& instance) noexcept {
    return instance.process_id != 0
        && instance.creation_identity != 0
        && !instance.executable_path.empty();
}

bool same_process_instance(const ProcessInstance& left, const ProcessInstance& right) noexcept {
    return is_valid_process_instance(left)
        && is_valid_process_instance(right)
        && left.process_id == right.process_id
        && left.creation_identity == right.creation_identity;
}

ProcessSample process_sample_absent(std::string message) {
    ProcessSample sample{};
    sample.kind = ProcessSampleKind::Absent;
    sample.message = std::move(message);
    return sample;
}

ProcessSample process_sample_present(ProcessInstance instance, std::string message) {
    ProcessSample sample{};
    sample.kind = ProcessSampleKind::Present;
    sample.instance = std::move(instance);
    sample.message = std::move(message);
    return sample;
}

ProcessSample process_sample_error(std::string message) {
    ProcessSample sample{};
    sample.kind = ProcessSampleKind::Error;
    sample.message = std::move(message);
    return sample;
}

ProcessObservation advance_process_lifecycle(const ProcessObservation& previous,
                                             const ProcessSample& sample) {
    ProcessObservation next{};

    if (sample.kind == ProcessSampleKind::Error) {
        next.state = ProcessLifecycle::ObservationError;
        next.instance = {};
        next.instance_changed = false;
        next.message = sample.message.empty() ? "Process observation failed." : sample.message;
        return next;
    }

    if (sample.kind == ProcessSampleKind::Present) {
        if (!is_valid_process_instance(sample.instance)) {
            next.state = ProcessLifecycle::ObservationError;
            next.instance = {};
            next.instance_changed = false;
            next.message = sample.message.empty()
                ? "Process sample is present but identity is incomplete."
                : sample.message;
            return next;
        }

        const bool same = previous.state == ProcessLifecycle::ProcessDetected
            && same_process_instance(previous.instance, sample.instance);
        next.state = ProcessLifecycle::ProcessDetected;
        next.instance = sample.instance;
        next.instance_changed = !same;
        next.message = sample.message.empty() ? "Matching process detected." : sample.message;
        return next;
    }

    // Absent
    if (previous.state == ProcessLifecycle::ProcessDetected
        && is_valid_process_instance(previous.instance)) {
        next.state = ProcessLifecycle::ProcessExited;
        next.instance = previous.instance;
        next.instance_changed = true;
        next.message = sample.message.empty() ? "Previously observed process is no longer present."
                                              : sample.message;
        return next;
    }

    next.state = ProcessLifecycle::NotRunning;
    next.instance = {};
    next.instance_changed = false;
    next.message = sample.message.empty() ? "No matching process is running." : sample.message;
    return next;
}

} // namespace deflux::core
