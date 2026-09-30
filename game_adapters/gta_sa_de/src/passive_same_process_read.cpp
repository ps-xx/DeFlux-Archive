#include "deflux/game/gta_sa_de/passive_same_process_read.hpp"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>

namespace deflux::game::gta_sa_de {
namespace {

bool address_readable(const void* address) noexcept {
    if (address == nullptr) {
        return false;
    }
    MEMORY_BASIC_INFORMATION info{};
    if (VirtualQuery(address, &info, sizeof(info)) != sizeof(info)) {
        return false;
    }
    if (info.State != MEM_COMMIT) {
        return false;
    }
    const DWORD protect = info.Protect & ~(PAGE_GUARD | PAGE_NOCACHE | PAGE_WRITECOMBINE);
    if (protect == PAGE_NOACCESS || protect == 0) {
        return false;
    }
    return (protect & (PAGE_READONLY | PAGE_READWRITE | PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE))
        != 0;
}

bool read_byte_seh(const void* address, std::uint8_t& out) noexcept {
#if defined(_MSC_VER)
    __try {
        out = *static_cast<const std::uint8_t*>(address);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
#else
    (void)address;
    (void)out;
    return false;
#endif
}

} // namespace

PassiveReadResult passive_read_byte(const void* address) noexcept {
    PassiveReadResult result{};
    if (!address_readable(address)) {
        result.failure_reason = "Address is not readable in the current process.";
        return result;
    }
    if (!read_byte_seh(address, result.byte)) {
        result.failure_reason = "Same-process read fault contained (access violation).";
        return result;
    }
    result.ok = true;
    return result;
}

} // namespace deflux::game::gta_sa_de
