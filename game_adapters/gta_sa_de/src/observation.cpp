#include "observation.hpp"

#include "discovery.hpp"
#include "process_query.hpp"
#include "win_utf8.hpp"

namespace deflux::game::gta_sa_de {

bool executable_paths_match(std::string_view utf8_left, std::string_view utf8_right) {
    const std::wstring left = detail::utf8_to_wide(utf8_left);
    const std::wstring right = detail::utf8_to_wide(utf8_right);
    if (left.empty() || right.empty()) {
        return false;
    }
    return detail::executable_paths_equal(left, right);
}

deflux::core::ProcessSample sample_live_process() {
    const std::wstring exe = detail::discover_gta_sa_de_executable();
    if (exe.empty()) {
        return deflux::core::process_sample_absent(
            "No verified installation executable; cannot match a running process.");
    }

    const auto queried = detail::query_verified_process(exe);
    switch (queried.status) {
    case detail::VerifiedQueryStatus::Found: {
        deflux::core::ProcessInstance instance{};
        instance.process_id = queried.process_id;
        instance.creation_identity = queried.creation_identity;
        instance.executable_path = detail::wide_to_utf8(queried.image_path);
        if (!deflux::core::is_valid_process_instance(instance)) {
            return deflux::core::process_sample_error(
                "Verified process query returned an incomplete process identity.");
        }
        return deflux::core::process_sample_present(std::move(instance), queried.message);
    }
    case detail::VerifiedQueryStatus::UnverifiedCandidate:
        return deflux::core::process_sample_error(queried.message);
    case detail::VerifiedQueryStatus::EnumerationFailed:
        return deflux::core::process_sample_error(queried.message);
    case detail::VerifiedQueryStatus::NotFound:
        return deflux::core::process_sample_absent(queried.message);
    }
    return deflux::core::process_sample_error("Unexpected process query status.");
}

} // namespace deflux::game::gta_sa_de
