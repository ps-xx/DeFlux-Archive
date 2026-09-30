#include "sha256.hpp"

#include "win_utf8.hpp"

#include <bcrypt.h>

#include <array>
#include <vector>

#pragma comment(lib, "bcrypt.lib")

namespace deflux::game::gta_sa_de::detail {
namespace {

void append_hex(const std::uint8_t* data, std::size_t length, std::string& out) {
    static constexpr char kDigits[] = "0123456789abcdef";
    out.resize(length * 2);
    for (std::size_t i = 0; i < length; ++i) {
        out[i * 2] = kDigits[(data[i] >> 4) & 0x0F];
        out[i * 2 + 1] = kDigits[data[i] & 0x0F];
    }
}

struct ScopedFile {
    HANDLE handle = INVALID_HANDLE_VALUE;
    ~ScopedFile() {
        if (handle != INVALID_HANDLE_VALUE) {
            CloseHandle(handle);
        }
    }
};

bool sha256_digest(const std::uint8_t* data, std::size_t size, std::string& hex_out) {
    hex_out.clear();
    if (data == nullptr || size == 0) {
        return false;
    }

    BCRYPT_ALG_HANDLE algorithm = nullptr;
    NTSTATUS status = BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0);
    if (!BCRYPT_SUCCESS(status) || algorithm == nullptr) {
        return false;
    }

    BCRYPT_HASH_HANDLE hash = nullptr;
    status = BCryptCreateHash(algorithm, &hash, nullptr, 0, nullptr, 0, 0);
    if (!BCRYPT_SUCCESS(status) || hash == nullptr) {
        BCryptCloseAlgorithmProvider(algorithm, 0);
        return false;
    }

    status = BCryptHashData(hash, const_cast<PUCHAR>(data), static_cast<ULONG>(size), 0);
    bool ok = BCRYPT_SUCCESS(status);
    std::array<std::uint8_t, 32> digest{};
    if (ok) {
        status = BCryptFinishHash(hash, digest.data(), static_cast<ULONG>(digest.size()), 0);
        ok = BCRYPT_SUCCESS(status);
    }

    BCryptDestroyHash(hash);
    BCryptCloseAlgorithmProvider(algorithm, 0);
    if (!ok) {
        return false;
    }

    append_hex(digest.data(), digest.size(), hex_out);
    return true;
}

} // namespace

bool sha256_buffer(const std::uint8_t* data, std::size_t size, std::string& hex_out) noexcept {
    return sha256_digest(data, size, hex_out);
}

bool sha256_file(std::wstring_view path, std::string& hex_out, std::uint64_t& size_out) {
    hex_out.clear();
    size_out = 0;

    if (path.empty()) {
        return false;
    }

    ScopedFile file;
    file.handle = CreateFileW(
        std::wstring(path).c_str(),
        GENERIC_READ,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        nullptr,
        OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL | FILE_FLAG_SEQUENTIAL_SCAN,
        nullptr);
    if (file.handle == INVALID_HANDLE_VALUE) {
        return false;
    }

    LARGE_INTEGER size{};
    if (!GetFileSizeEx(file.handle, &size) || size.QuadPart < 0) {
        return false;
    }
    size_out = static_cast<std::uint64_t>(size.QuadPart);

    BCRYPT_ALG_HANDLE algorithm = nullptr;
    NTSTATUS status = BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0);
    if (!BCRYPT_SUCCESS(status) || algorithm == nullptr) {
        return false;
    }

    BCRYPT_HASH_HANDLE hash = nullptr;
    status = BCryptCreateHash(algorithm, &hash, nullptr, 0, nullptr, 0, 0);
    if (!BCRYPT_SUCCESS(status) || hash == nullptr) {
        BCryptCloseAlgorithmProvider(algorithm, 0);
        return false;
    }

    std::vector<std::uint8_t> buffer(1024 * 1024);
    bool ok = true;
    for (;;) {
        DWORD read = 0;
        if (!ReadFile(file.handle, buffer.data(), static_cast<DWORD>(buffer.size()), &read, nullptr)) {
            ok = false;
            break;
        }
        if (read == 0) {
            break;
        }
        status = BCryptHashData(hash, buffer.data(), read, 0);
        if (!BCRYPT_SUCCESS(status)) {
            ok = false;
            break;
        }
    }

    std::array<std::uint8_t, 32> digest{};
    if (ok) {
        status = BCryptFinishHash(hash, digest.data(), static_cast<ULONG>(digest.size()), 0);
        ok = BCRYPT_SUCCESS(status);
    }

    BCryptDestroyHash(hash);
    BCryptCloseAlgorithmProvider(algorithm, 0);

    if (!ok) {
        size_out = 0;
        return false;
    }

    append_hex(digest.data(), digest.size(), hex_out);
    return true;
}

} // namespace deflux::game::gta_sa_de::detail
