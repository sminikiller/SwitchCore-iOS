// SPDX-License-Identifier: GPL-3.0-only
#include "core/hle/display.hpp"
#include <chrono>
#include <iostream>

int main() {
    using namespace NeXo2;
    constexpr u32 width = 1280, height = 720, pitch = width * 4;
    constexpr u64 base = 0x80000000;
    Core::Memory memory;
    std::vector<u8> bytes(pitch * 768);
    for (size_t i = 0; i < bytes.size(); ++i)
        bytes[i] = static_cast<u8>(i * 7 + (i >> 8));
    memory.MapRegion(base, bytes.size(), Core::MemoryState::Normal, Core::MemoryPermission::ReadWrite, "fb");
    memory.WriteBytes(base, bytes.data(), bytes.size());
    for (u32 layout : {1u, 3u}) {
        HLE::Display display;
        HLE::GraphicBufferInfo info;
        info.width = width;
        info.height = height;
        info.pitch = pitch;
        info.block_height_log2 = 4;
        info.format = 1;
        info.layout = layout;
        std::vector<double> times;
        for (int frame = 0; frame < 210; ++frame) {
            const auto start = std::chrono::steady_clock::now();
            if (!display.PresentAt(memory, info, base))
                return 1;
            if (frame >= 10)
                times.push_back(
                    std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start)
                        .count());
        }
        u64 checksum = 0;
        for (auto pixel : display.Frame().rgba)
            checksum = checksum * 31 + pixel;
        std::sort(times.begin(), times.end());
        std::cout << (layout == 1 ? "pitch" : "block-linear") << " 1280x720 RGBA8 n=" << times.size()
                  << " median_ms=" << times[99] << " p95_ms=" << times[189] << " checksum=" << checksum
                  << '\n';
    }
}
