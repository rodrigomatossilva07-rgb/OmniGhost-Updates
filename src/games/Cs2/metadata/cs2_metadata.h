#pragma once

#include "../config/cs2_config.h"

#include <cstddef>

namespace CS2::Metadata {

[[nodiscard]] bool IsBotPlayerName(const char* name) noexcept;
[[nodiscard]] bool IsDroppedWeaponClass(const char* class_name) noexcept;
[[nodiscard]] bool DroppedWeaponAllowed(const char* class_name,
                                        const Config& settings) noexcept;
void FormatDroppedWeaponName(const char* source, char* output,
                             std::size_t output_size) noexcept;

} // namespace CS2::Metadata
