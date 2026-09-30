#include "deflux/game/gta_sa_de/detection.hpp"

#include "discovery.hpp"
#include "fingerprint.hpp"
#include "process_query.hpp"
#include "supported_builds.hpp"
#include "win_utf8.hpp"

namespace deflux::game::gta_sa_de {
namespace {

DetectionSnapshot snapshot_from_exe(const std::wstring& exe, bool require_layout, const char* found_how) {
    DetectionSnapshot snap{};

    if (exe.empty()) {
        snap.installation = InstallationStatus::NotFound;
        snap.compatibility = Compatibility::NotFound;
        snap.message = "GTA SA DE executable was not found.";
        return snap;
    }

    const auto kind = detail::classify_path(exe);
    if (kind == detail::PathKind::Missing) {
        snap.installation = InstallationStatus::NotFound;
        snap.compatibility = Compatibility::NotFound;
        snap.message = "GTA SA DE executable path does not exist.";
        return snap;
    }
    if (kind == detail::PathKind::NotAFile) {
        snap.installation = InstallationStatus::Error;
        snap.compatibility = Compatibility::Error;
        snap.executable_path = detail::wide_to_utf8(exe);
        snap.message = "Path exists but is not a regular executable file.";
        return snap;
    }

    const std::wstring canonical = detail::canonicalize_file_path(exe);
    if (canonical.empty()) {
        snap.installation = InstallationStatus::Error;
        snap.compatibility = Compatibility::Error;
        snap.message = "Failed to canonicalize executable path.";
        return snap;
    }

    if (require_layout && !detail::looks_like_gta_sa_de_layout(canonical)) {
        snap.installation = InstallationStatus::Error;
        snap.compatibility = Compatibility::Error;
        snap.executable_path = detail::wide_to_utf8(canonical);
        snap.message = "Path is not a GTA SA DE Gameface\\Binaries\\Win64 executable.";
        return snap;
    }

    const auto fp = detail::fingerprint_executable(canonical);
    snap.executable_path = detail::wide_to_utf8(canonical);
    snap.file_size_bytes = fp.size_bytes;
    snap.sha256_hex = fp.sha256_hex;
    snap.file_version = fp.file_version;
    snap.product_version = fp.product_version;

    if (!fp.ok || snap.sha256_hex.size() != 64) {
        snap.installation = InstallationStatus::Error;
        snap.compatibility = Compatibility::Error;
        snap.sha256_hex.clear();
        snap.message = "Failed to SHA-256 fingerprint the executable.";
        return snap;
    }

    snap.installation = InstallationStatus::Found;
    const bool registered = detail::fingerprint_is_registered(snap.sha256_hex);
    snap.compatibility = compatibility_of(snap.installation, true, registered);

    const auto running = detail::find_running_gta_sa_de(canonical);
    snap.running = running.running;
    snap.process_id = running.running ? running.process_id : 0;

    snap.message = found_how;
    if (snap.compatibility == Compatibility::DetectedButUnsupported) {
        snap.message += " Fingerprint is not in the supported-build registry.";
    } else if (snap.compatibility == Compatibility::Supported) {
        snap.message += " Fingerprint is registered as supported.";
    }
    return snap;
}

} // namespace

const char* to_string(InstallationStatus status) noexcept {
    switch (status) {
    case InstallationStatus::Found:
        return "FOUND";
    case InstallationStatus::NotFound:
        return "NOT_FOUND";
    case InstallationStatus::Error:
        return "ERROR";
    }
    return "UNKNOWN";
}

const char* to_string(Compatibility status) noexcept {
    switch (status) {
    case Compatibility::Supported:
        return "SUPPORTED";
    case Compatibility::DetectedButUnsupported:
        return "DETECTED_BUT_UNSUPPORTED";
    case Compatibility::NotFound:
        return "NOT_FOUND";
    case Compatibility::Error:
        return "ERROR";
    }
    return "UNKNOWN";
}

bool is_supported_fingerprint(std::string_view sha256_hex) noexcept {
    return detail::fingerprint_is_registered(sha256_hex);
}

Compatibility compatibility_of(InstallationStatus installation,
                               bool fingerprint_ok,
                               bool fingerprint_registered) noexcept {
    if (installation == InstallationStatus::Error) {
        return Compatibility::Error;
    }
    if (installation == InstallationStatus::NotFound) {
        return Compatibility::NotFound;
    }
    if (!fingerprint_ok) {
        return Compatibility::Error;
    }
    if (fingerprint_registered) {
        return Compatibility::Supported;
    }
    return Compatibility::DetectedButUnsupported;
}

ProbeStatus to_probe_status(Compatibility compatibility) noexcept {
    switch (compatibility) {
    case Compatibility::Supported:
        return ProbeStatus::Supported;
    case Compatibility::DetectedButUnsupported:
        return ProbeStatus::Unsupported;
    case Compatibility::NotFound:
        return ProbeStatus::Unavailable;
    case Compatibility::Error:
        return ProbeStatus::Failed;
    }
    return ProbeStatus::Failed;
}

DetectionSnapshot detect_installation() {
    const std::wstring exe = detail::discover_gta_sa_de_executable();
    if (!exe.empty()) {
        return snapshot_from_exe(exe, true, "Located via Steam library metadata.");
    }

    const auto running = detail::find_running_gta_sa_de({});
    if (running.running && !running.image_path.empty()) {
        auto snap = snapshot_from_exe(running.image_path, true, "Located from a running process image path.");
        snap.running = true;
        snap.process_id = running.process_id;
        return snap;
    }

    DetectionSnapshot snap{};
    snap.installation = InstallationStatus::NotFound;
    snap.compatibility = Compatibility::NotFound;
    snap.message = "No GTA SA DE installation was discovered and no matching process is running.";
    return snap;
}

DetectionSnapshot detect_explicit_executable(std::string_view utf8_path) {
    if (utf8_path.empty()) {
        DetectionSnapshot snap{};
        snap.installation = InstallationStatus::NotFound;
        snap.compatibility = Compatibility::NotFound;
        snap.message = "No executable path was provided.";
        return snap;
    }

    const std::wstring wide = detail::utf8_to_wide(utf8_path);
    if (wide.empty()) {
        DetectionSnapshot snap{};
        snap.installation = InstallationStatus::Error;
        snap.compatibility = Compatibility::Error;
        snap.message = "Executable path is not valid UTF-8.";
        return snap;
    }

    // Explicit probe is for tests/manual use and may fingerprint any regular file.
    return snapshot_from_exe(wide, false, "Fingerprinted explicit executable path.");
}

} // namespace deflux::game::gta_sa_de
