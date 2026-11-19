// SPDX-License-Identifier: GPL-3.0-only
#include "core/runtime/SwitchCore.h"
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <sstream>
#include <vector>

// Each line holds a guest frame count, a decimal HID button mask and signed left-stick X.
// Captures the actual guest image at each checkpoint. No polling of host input or host display.
int main(int argc, char **argv) {
    if (argc != 5) {
        std::cerr << "switchcore-replay PROGRAM TITLE_DIR PLAN.txt CAPTURE_DIR\n";
        return 2;
    }
    std::ifstream plan(argv[3]);
    if (!plan)
        return 2;
    std::unique_ptr<SCSession, decltype(&sc_destroy)> session(sc_create(), sc_destroy);
    if (!sc_load(session.get(), SC_SWITCH_1, argv[1], argv[2])) {
        std::cerr << sc_message(session.get()) << '\n';
        return 1;
    }
    std::filesystem::path output(argv[4]);
    std::filesystem::create_directories(output);
    uint64_t checkpoint = 0, captured = 0;
    SCStats stats{};
    for (std::string line; std::getline(plan, line);) {
        if (line.empty() || line[0] == '#')
            continue;
        std::istringstream input(line);
        int frames, stick;
        uint64_t buttons;
        if (!(input >> frames >> buttons >> stick) || frames < 1 || frames > 3600 || stick < -32768 ||
            stick > 32767)
            return 2;
        checkpoint += frames;
        sc_pad(session.get(), buttons, stick, 0, 0, 0);
        const auto start = std::chrono::steady_clock::now();
        do {
            stats = sc_step(session.get(), 100000);
        } while (stats.state == SC_RUNNING && stats.frames < checkpoint &&
                 std::chrono::steady_clock::now() - start < std::chrono::seconds(120));
        const auto elapsed = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
        std::cout << "target=" << checkpoint << " frames=" << stats.frames << " elapsed_s=" << elapsed
                  << " instructions=" << stats.instructions << " pc=" << std::hex << stats.pc << std::dec
                  << " status=" << sc_message(session.get()) << std::endl;
        if (stats.frames > captured) {
            std::ofstream ppm(output / ("frame-" + std::to_string(stats.frames) + ".ppm"), std::ios::binary);
            auto save = [](void *ctx, const uint32_t *pixels, uint32_t w, uint32_t h) {
                auto &out = *static_cast<std::ofstream *>(ctx);
                out << "P6\n" << w << ' ' << h << "\n255\n";
                std::vector<uint8_t> rgb(size_t(w) * 3);
                for (uint32_t y = 0; y < h; ++y) {
                    for (uint32_t x = 0; x < w; ++x) {
                        const auto color = *pixels++;
                        rgb[x * 3] = static_cast<uint8_t>(color);
                        rgb[x * 3 + 1] = static_cast<uint8_t>(color >> 8);
                        rgb[x * 3 + 2] = static_cast<uint8_t>(color >> 16);
                    }
                    out.write(reinterpret_cast<const char *>(rgb.data()), rgb.size());
                }
            };
            if (!sc_frame(session.get(), captured, save, &ppm, &captured))
                return 1;
            ppm.close();
            if (!ppm)
                return 1;
        }
        if (stats.state == SC_FAULT || stats.frames < checkpoint) {
            std::cerr << sc_log(session.get()) << '\n';
            return 1;
        }
    }
    return checkpoint && !plan.bad() ? 0 : 2;
}
