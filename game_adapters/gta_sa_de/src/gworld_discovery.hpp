#pragma once

#include "deflux/game/gta_sa_de/qualification.hpp"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace deflux::game::gta_sa_de {

struct PeSectionView {
    char name[9]{};
    std::uint32_t virtual_address = 0;
    std::uint32_t virtual_size = 0;
    std::uint32_t raw_pointer = 0;
    std::uint32_t raw_size = 0;
    std::uint32_t characteristics = 0;
    bool executable = false;
};

GWorldDiscoveryResult discover_gworld_slots_in_image(const std::uint8_t* image,
                                                     std::size_t image_size,
                                                     std::uint32_t size_of_image) noexcept;

std::uint32_t decode_x64_rip_relative(std::uint32_t instruction_rva,
                                      const std::uint8_t* instruction_bytes,
                                      std::uint32_t displacement_offset,
                                      std::uint32_t instruction_length) noexcept;

bool match_signature(const std::uint8_t* data,
                     std::size_t size,
                     const std::uint8_t* pattern,
                     const char* mask) noexcept;

bool pe_enumerate_sections(const std::uint8_t* headers,
                           std::size_t header_size,
                           std::vector<PeSectionView>& out) noexcept;

bool pe_section_for_rva(const std::vector<PeSectionView>& sections,
                        std::uint32_t rva,
                        PeSectionView& out) noexcept;

} // namespace deflux::game::gta_sa_de
