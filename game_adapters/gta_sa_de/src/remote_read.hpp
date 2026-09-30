#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>

#include <cstddef>
#include <cstdint>
#include <string>

namespace deflux::game::gta_sa_de::detail {

class ScopedProcess {
public:
    ScopedProcess() = default;
    ScopedProcess(const ScopedProcess&) = delete;
    ScopedProcess& operator=(const ScopedProcess&) = delete;

    ~ScopedProcess() { close(); }

    bool open_read_only(std::uint32_t process_id) noexcept {
        close();
        handle_ = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION | PROCESS_VM_READ,
                              FALSE,
                              static_cast<DWORD>(process_id));
        return handle_ != nullptr;
    }

    void close() noexcept {
        if (handle_ != nullptr && handle_ != INVALID_HANDLE_VALUE) {
            CloseHandle(handle_);
        }
        handle_ = nullptr;
    }

    HANDLE native() const noexcept { return handle_; }

    bool read_exact(std::uint64_t address, void* out, std::size_t bytes) const noexcept {
        if (handle_ == nullptr || out == nullptr || bytes == 0 || address == 0) {
            return false;
        }
        SIZE_T copied = 0;
        if (!ReadProcessMemory(handle_,
                               reinterpret_cast<LPCVOID>(address),
                               out,
                               bytes,
                               &copied)) {
            return false;
        }
        return copied == bytes;
    }

    bool query_identity(std::uint32_t& process_id,
                        std::uint64_t& creation_identity,
                        std::wstring& image_path) const;

private:
    HANDLE handle_ = nullptr;
};

} // namespace deflux::game::gta_sa_de::detail
