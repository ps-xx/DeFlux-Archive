#include "deflux/core/process_lifecycle.hpp"

#include <cstdio>
#include <cstring>
#include <string>

namespace deflux_tests {
namespace {

int g_failed = 0;
int g_passed = 0;

void check(bool condition, const char* expression, const char* file, int line) {
    if (condition) {
        ++g_passed;
        return;
    }
    ++g_failed;
    std::printf("FAIL %s:%d: %s\n", file, line, expression);
}

#define CHECK_LIFE(expr) check(static_cast<bool>(expr), #expr, __FILE__, __LINE__)

deflux::core::ProcessInstance make_instance(std::uint32_t pid,
                                            std::uint64_t created,
                                            const char* path) {
    deflux::core::ProcessInstance instance{};
    instance.process_id = pid;
    instance.creation_identity = created;
    instance.executable_path = path;
    return instance;
}

void test_lifecycle_names_and_validity() {
    using deflux::core::ProcessLifecycle;
    using deflux::core::to_string;

    CHECK_LIFE(std::strcmp(to_string(ProcessLifecycle::NotRunning), "NOT_RUNNING") == 0);
    CHECK_LIFE(std::strcmp(to_string(ProcessLifecycle::ProcessDetected), "PROCESS_DETECTED") == 0);
    CHECK_LIFE(std::strcmp(to_string(ProcessLifecycle::ProcessExited), "PROCESS_EXITED") == 0);
    CHECK_LIFE(std::strcmp(to_string(ProcessLifecycle::ObservationError), "OBSERVATION_ERROR") == 0);

    CHECK_LIFE(!deflux::core::is_valid_process_instance({}));
    CHECK_LIFE(!deflux::core::is_valid_process_instance(make_instance(0, 1, "x")));
    CHECK_LIFE(!deflux::core::is_valid_process_instance(make_instance(1, 0, "x")));
    CHECK_LIFE(!deflux::core::is_valid_process_instance(make_instance(1, 1, "")));
    CHECK_LIFE(deflux::core::is_valid_process_instance(make_instance(1, 1, "x")));

    CHECK_LIFE(!deflux::core::same_process_instance(make_instance(1, 1, "a"), make_instance(1, 2, "a")));
    CHECK_LIFE(!deflux::core::same_process_instance(make_instance(1, 1, "a"), make_instance(2, 1, "a")));
    CHECK_LIFE(deflux::core::same_process_instance(make_instance(4, 9, "a"), make_instance(4, 9, "b")));
}

void test_lifecycle_transitions() {
    using deflux::core::ProcessLifecycle;
    using deflux::core::ProcessObservation;
    using deflux::core::advance_process_lifecycle;
    using deflux::core::process_sample_absent;
    using deflux::core::process_sample_error;
    using deflux::core::process_sample_present;

    ProcessObservation previous{};
    CHECK_LIFE(previous.state == ProcessLifecycle::NotRunning);

    auto obs = advance_process_lifecycle(previous, process_sample_absent("none"));
    CHECK_LIFE(obs.state == ProcessLifecycle::NotRunning);
    CHECK_LIFE(obs.instance.process_id == 0);
    CHECK_LIFE(!obs.instance_changed);

    const auto first = make_instance(100, 1000, "/game/bin/app");
    obs = advance_process_lifecycle(obs, process_sample_present(first, "seen"));
    CHECK_LIFE(obs.state == ProcessLifecycle::ProcessDetected);
    CHECK_LIFE(obs.instance_changed);
    CHECK_LIFE(obs.instance.process_id == 100);
    CHECK_LIFE(obs.instance.creation_identity == 1000);

    const auto same = advance_process_lifecycle(obs, process_sample_present(first, "again"));
    CHECK_LIFE(same.state == ProcessLifecycle::ProcessDetected);
    CHECK_LIFE(!same.instance_changed);
    CHECK_LIFE(same.instance.process_id == 100);

    const auto exited = advance_process_lifecycle(same, process_sample_absent("gone"));
    CHECK_LIFE(exited.state == ProcessLifecycle::ProcessExited);
    CHECK_LIFE(exited.instance_changed);
    CHECK_LIFE(exited.instance.process_id == 100);

    const auto idle = advance_process_lifecycle(exited, process_sample_absent("still gone"));
    CHECK_LIFE(idle.state == ProcessLifecycle::NotRunning);
    CHECK_LIFE(!idle.instance_changed);
    CHECK_LIFE(idle.instance.process_id == 0);

    const auto again = make_instance(200, 2000, "/game/bin/app");
    const auto next = advance_process_lifecycle(idle, process_sample_present(again, "new"));
    CHECK_LIFE(next.state == ProcessLifecycle::ProcessDetected);
    CHECK_LIFE(next.instance_changed);
    CHECK_LIFE(next.instance.process_id == 200);

    const auto reused = make_instance(200, 3000, "/game/bin/app");
    const auto pid_reuse = advance_process_lifecycle(next, process_sample_present(reused, "reuse"));
    CHECK_LIFE(pid_reuse.state == ProcessLifecycle::ProcessDetected);
    CHECK_LIFE(pid_reuse.instance_changed);
    CHECK_LIFE(pid_reuse.instance.creation_identity == 3000);

    const auto other_pid = make_instance(201, 3000, "/game/bin/app");
    const auto different_pid = advance_process_lifecycle(pid_reuse, process_sample_present(other_pid, "pid"));
    CHECK_LIFE(different_pid.instance_changed);
    CHECK_LIFE(different_pid.instance.process_id == 201);

    const auto invalid = advance_process_lifecycle(
        different_pid, process_sample_present(make_instance(0, 1, "x"), "bad"));
    CHECK_LIFE(invalid.state == ProcessLifecycle::ObservationError);
    CHECK_LIFE(invalid.instance.process_id == 0);

    const auto errored = advance_process_lifecycle(invalid, process_sample_error("enum failed"));
    CHECK_LIFE(errored.state == ProcessLifecycle::ObservationError);
    CHECK_LIFE(std::strcmp(deflux::core::to_string(errored.state), "OBSERVATION_ERROR") == 0);
    CHECK_LIFE(errored.message.find("enum failed") != std::string::npos);

    const auto recovered = advance_process_lifecycle(errored, process_sample_absent("none"));
    CHECK_LIFE(recovered.state == ProcessLifecycle::NotRunning);
}

} // namespace

int run_process_lifecycle_tests(int& passed, int& failed) {
    const int passed_before = g_passed;
    const int failed_before = g_failed;
    test_lifecycle_names_and_validity();
    test_lifecycle_transitions();
    passed += (g_passed - passed_before);
    failed += (g_failed - failed_before);
    return g_failed - failed_before;
}

} // namespace deflux_tests
