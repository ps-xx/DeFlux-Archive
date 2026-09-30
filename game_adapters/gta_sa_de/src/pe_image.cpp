#include "pe_image.hpp"

namespace deflux::game::gta_sa_de::detail {

constexpr std::uint16_t kDosMagic = 0x5A4D;
constexpr std::uint32_t kNtSignature = 0x00004550;
constexpr std::uint16_t kMachineAmd64 = 0x8664;
constexpr std::uint16_t kPe32PlusMagic = 0x020B;
constexpr std::uint32_t kMinImageSize = 0x1000u;
constexpr std::uint32_t kMaxImageSize = 0x40000000u;

bool read_u16(const std::uint8_t* data, std::size_t size, std::size_t offset, std::uint16_t& out) noexcept {
    if (data == nullptr || offset + 2 > size) {
        return false;
    }
    out = static_cast<std::uint16_t>(data[offset] | (static_cast<std::uint16_t>(data[offset + 1]) << 8));
    return true;
}

bool read_u32(const std::uint8_t* data, std::size_t size, std::size_t offset, std::uint32_t& out) noexcept {
    if (data == nullptr || offset + 4 > size) {
        return false;
    }
    out = static_cast<std::uint32_t>(data[offset])
        | (static_cast<std::uint32_t>(data[offset + 1]) << 8)
        | (static_cast<std::uint32_t>(data[offset + 2]) << 16)
        | (static_cast<std::uint32_t>(data[offset + 3]) << 24);
    return true;
}

} // namespace deflux::game::gta_sa_de::detail

namespace deflux::game::gta_sa_de {

PeParseStatus parse_pe64_headers(const std::uint8_t* data, std::size_t size, PeImageInfo& out) noexcept {
    out = {};
    if (data == nullptr || size == 0) {
        return PeParseStatus::InvalidArgument;
    }
    if (size < 0x40) {
        return PeParseStatus::TooSmall;
    }

    std::uint16_t dos = 0;
    if (!detail::read_u16(data, size, 0, dos) || dos != detail::kDosMagic) {
        return PeParseStatus::BadDosMagic;
    }

    std::uint32_t lfanew = 0;
    if (!detail::read_u32(data, size, 0x3C, lfanew) || lfanew < 0x40 || lfanew > size - 24) {
        return PeParseStatus::BadNewHeaderOffset;
    }

    std::uint32_t sig = 0;
    if (!detail::read_u32(data, size, lfanew, sig) || sig != detail::kNtSignature) {
        return PeParseStatus::BadNtSignature;
    }

    std::uint16_t machine = 0;
    std::uint16_t sections = 0;
    std::uint16_t optional_size = 0;
    std::uint16_t magic = 0;
    if (!detail::read_u16(data, size, lfanew + 4, machine)
        || !detail::read_u16(data, size, lfanew + 6, sections)
        || !detail::read_u16(data, size, lfanew + 20, optional_size)
        || !detail::read_u16(data, size, lfanew + 24, magic)) {
        return PeParseStatus::TooSmall;
    }
    if (machine != detail::kMachineAmd64) {
        return PeParseStatus::NotAmd64;
    }
    if (magic != detail::kPe32PlusMagic) {
        return PeParseStatus::NotPe32Plus;
    }

    std::uint32_t size_of_image = 0;
    std::uint32_t size_of_headers = 0;
    if (!detail::read_u32(data, size, lfanew + 80, size_of_image)
        || !detail::read_u32(data, size, lfanew + 84, size_of_headers)) {
        return PeParseStatus::TooSmall;
    }
    if (size_of_image < detail::kMinImageSize || size_of_image > detail::kMaxImageSize
        || size_of_headers < 0x40 || size_of_headers > size_of_image) {
        return PeParseStatus::ImplausibleImageSize;
    }

    out.size_of_image = size_of_image;
    out.size_of_headers = size_of_headers;
    out.machine = machine;
    out.number_of_sections = sections;
    out.pe32_plus = true;
    return PeParseStatus::Ok;
}

bool pe_rva_in_non_executable_section(const std::uint8_t* data, std::size_t size, std::uint32_t rva) noexcept {
    PeImageInfo info{};
    if (parse_pe64_headers(data, size, info) != PeParseStatus::Ok) {
        return false;
    }

    std::uint32_t lfanew = 0;
    std::uint16_t optional_size = 0;
    if (!detail::read_u32(data, size, 0x3C, lfanew)
        || !detail::read_u16(data, size, lfanew + 20, optional_size)) {
        return false;
    }

    const std::size_t section_table = static_cast<std::size_t>(lfanew) + 24u + optional_size;
    constexpr std::size_t kSection = 40;
    for (std::uint16_t i = 0; i < info.number_of_sections; ++i) {
        const std::size_t off = section_table + static_cast<std::size_t>(i) * kSection;
        std::uint32_t virtual_size = 0;
        std::uint32_t virtual_address = 0;
        std::uint32_t raw_size = 0;
        std::uint32_t characteristics = 0;
        if (!detail::read_u32(data, size, off + 8, virtual_size)
            || !detail::read_u32(data, size, off + 12, virtual_address)
            || !detail::read_u32(data, size, off + 16, raw_size)
            || !detail::read_u32(data, size, off + 36, characteristics)) {
            return false;
        }
        std::uint32_t span = virtual_size != 0 ? virtual_size : raw_size;
        if (span == 0) {
            continue;
        }
        if (rva < virtual_address || rva >= virtual_address + span) {
            continue;
        }
        return (characteristics & detail::kScnMemExecute) == 0;
    }
    return false;
}

} // namespace deflux::game::gta_sa_de
