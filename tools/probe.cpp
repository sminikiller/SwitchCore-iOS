// SPDX-License-Identifier: GPL-3.0-only
#include "core/runtime/SwitchCore.h"
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <string>

struct Image {
    uint32_t width{}, height{}, corner{};
    std::string output;
};
void receive(void *context, const uint32_t *rgba, uint32_t width, uint32_t height) {
    auto &image = *static_cast<Image *>(context);
    image.width = width;
    image.height = height;
    image.corner = rgba[0];
    if (image.output.empty())
        return;
    std::ofstream stream(image.output, std::ios::binary);
    stream << "P6\n" << width << ' ' << height << "\n255\n";
    for (size_t p = 0; p < size_t(width) * height; ++p)
        stream.write(reinterpret_cast<const char *>(&rgba[p]), 3);
    image.output.clear();
}
int main(int argc, char **argv) {
    // A leading --jit runs the guest CPU on the ARM64 recompiler instead of the interpreter.
    // SWITCHCORE_JIT=1 does the same for callers with a fixed command line (verify-independent.py).
    const bool flag = argc > 1 && std::string(argv[1]) == "--jit";
    if (flag) {
        --argc;
        ++argv;
    }
    const char *variable = std::getenv("SWITCHCORE_JIT");
    const bool jit = flag || (variable && std::string(variable) == "1");
    if (argc < 3 || argc > 5) {
        std::cerr << "switchcore-probe [--jit] NRO_OR_EXTRACTED_FOLDER TITLE_DIR "
                     "[rally|soak|frame|exit|launcher] [capture.ppm]\n";
        return 2;
    }
    const std::string mode = argc > 3 ? argv[3] : "frame";
    if (mode != "frame" && mode != "rally" && mode != "soak" && mode != "exit" && mode != "launcher")
        return 2;
    std::unique_ptr<SCSession, decltype(&sc_destroy)> s(sc_create(), sc_destroy);
    if (jit && !sc_set_jit(s.get(), true)) {
        std::cerr << "Ricompilatore non disponibile in questa build.\n";
        return 1;
    }
    if (!sc_load(s.get(), SC_SWITCH_1, argv[1], argv[2])) {
        std::cerr << sc_message(s.get()) << '\n';
        return 1;
    }
    Image image;
    size_t audibleFrames = 0;
    sc_audio(
        s.get(),
        [](void *context, const int16_t *samples, size_t n) {
            auto &count = *static_cast<size_t *>(context);
            for (size_t i = 0; i < n; ++i)
                if (samples[2 * i] || samples[2 * i + 1])
                    ++count;
        },
        &audibleFrames);
    if (argc == 5)
        image.output = argv[4];
    SCStats stats{};
    uint64_t last = 0;
    bool neutral = false, pressed = false, released = false;
    // launcher: 0 menu, 1 A held in menu, 2 Rally as in rally mode, 3 menu again, 4 + held in menu
    int phase = 0, menuFrames = 0;
    const std::string menuTitle = sc_title(s.get());
    const auto limit = std::chrono::seconds(mode == "soak" ? 300 : mode == "launcher" ? 180 : 60);
    const auto start = std::chrono::steady_clock::now();
    do {
        uint64_t buttons =
            (released && (mode != "soak" || last >= 6000)) ? 1u << 10 : (neutral && !pressed ? 1 : 0);
        if (mode == "launcher" && phase != 2)
            buttons = phase == 1 ? 1 : phase == 4 ? 1u << 10 : 0;
        sc_pad(s.get(), buttons, 0, 0, 0, 0);
        stats = sc_step(s.get(), 100000);
        // Programs switch inside sc_step: release the button that requested the switch.
        const bool inMenu = menuTitle == sc_title(s.get());
        if (mode == "launcher" && ((phase == 1 && !inMenu) || (phase == 2 && inMenu)))
            ++phase;
        if (sc_frame(s.get(), last, receive, &image, &last)) {
            if (mode == "frame")
                break;
            if (mode == "rally" || mode == "soak" || phase == 2) {
                if (!neutral)
                    neutral = image.width == 320 && image.height == 180 && image.corner == 0xff352316;
                else if (!pressed)
                    pressed = image.corner == 0xff694c19;
                else if (!released)
                    released = image.corner == 0xff352316;
            }
            if (mode == "launcher" && (phase == 0 || phase == 3) && image.width == 1280 &&
                ++menuFrames == 5) {
                ++phase;
                menuFrames = 0;
            }
        }
    } while (stats.state == SC_RUNNING && std::chrono::steady_clock::now() - start < limit);
    const double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
    const SCJitStats recompiler = sc_jit_stats(s.get());
    std::cout << "title=" << sc_title(s.get()) << " instructions=" << stats.instructions
              << " guest_bytes=" << stats.guest_bytes << " frames=" << last << " size=" << image.width << "x"
              << image.height << " elapsed_s=" << seconds << " input=" << pressed << " release=" << released
              << " audible_frames=" << audibleFrames << " phase=" << phase
              << " cpu=" << (recompiler.active ? "jit" : "interpreter")
              << " mips=" << stats.instructions / seconds / 1e6 << " fallbacks=" << recompiler.fallbacks
              << " interpreted_words=" << recompiler.interpreted_words << std::hex
              << " code_base=" << recompiler.code_base << " pc=" << stats.pc << std::dec
              << " status=" << sc_message(s.get()) << '\n'
              << sc_log(s.get()) << '\n';
    std::error_code ec;
    auto save = std::filesystem::path(argv[2]) / "sdmc" / "rally-record.bin";
    const bool saved = std::filesystem::file_size(save, ec) == sizeof(uint32_t) && !ec;
    if (mode == "rally" || mode == "soak")
        return !(released && audibleFrames > 0 && saved && stats.state == SC_EXITED &&
                 (mode != "soak" || last >= 6000));
    if (mode == "launcher")
        return !(phase == 4 && released && audibleFrames > 0 && saved && stats.state == SC_EXITED);
    return mode == "exit" ? stats.state != SC_EXITED : last == 0;
}
