#pragma once

#include <cstdint>
#include <string>
#include <string_view>

namespace deflux::game::gta_sa_de::detail {

// SHA-256 of a regular file. hex_out is 64 lowercase hex chars on success.
bool sha256_file(std::wstring_view path, std::string& hex_out, std::uint64_t& size_out);

// SHA-256 of an in-memory buffer. hex_out is 64 lowercase hex chars on success.
bool sha256_buffer(const std::uint8_t* data, std::size_t size, std::string& hex_out) noexcept;

} // namespace deflux::game::gta_sa_de::detail
