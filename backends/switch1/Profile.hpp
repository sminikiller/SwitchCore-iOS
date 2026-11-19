#pragma once
#include <cstdint>
namespace switchcore::switch1 {
inline constexpr std::uint64_t counter_hz = 19'200'000;
inline constexpr int cores = 4;
inline constexpr std::uint64_t max_nro = 128ull << 20;
inline constexpr std::uint64_t max_image = 192ull << 20;
}

