// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <filesystem>
#include <optional>
#include <string>

namespace switchcore {
// Maps a guest file ("sdmc:/switch/game.nro" or "/switch/game.nro") into the title's virtual SD.
// Paths that resolve to the SD root or leave it through "..", links or another root are rejected.
inline std::optional<std::filesystem::path> sd_file(const std::filesystem::path &root, std::string guest) {
    namespace fs = std::filesystem;
    if (guest.rfind("sdmc:", 0) == 0)
        guest.erase(0, 5);
    std::error_code error;
    const auto base = fs::weakly_canonical(root, error);
    if (error)
        return std::nullopt;
    const auto target = fs::weakly_canonical(base / fs::u8path(guest).relative_path(), error);
    const auto inside = target.lexically_relative(base);
    if (error || inside.empty() || inside == "." || *inside.begin() == "..")
        return std::nullopt;
    return target;
}
} // namespace switchcore
