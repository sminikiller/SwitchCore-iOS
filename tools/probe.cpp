// SPDX-License-Identifier: GPL-3.0-only
#include "core/runtime/SwitchCore.h"
#include <chrono>
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
    if (argc < 3 || argc > 5) {
        std::cerr << "switchcore-probe FILE.nro TITLE_DIR [rally|soak|frame|exit] [capture.ppm]\n";
        return 2;
    }
    const std::string mode = argc > 3 ? argv[3] : "frame";
    if (mode != "frame" && mode != "rally" && mode != "soak" && mode != "exit")
        return 2;
    std::unique_ptr<SCSession, decltype(&sc_destroy)> s(sc_create(), sc_destroy);
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
    const auto start = std::chrono::steady_clock::now();
    do {
        sc_pad(s.get(),
               (released && (mode != "soak" || last >= 6000)) ? 1u << 10 : (neutral && !pressed ? 1 : 0), 0,
               0, 0, 0);
        stats = sc_step(s.get(), 100000);
        if (sc_frame(s.get(), last, receive, &image, &last)) {
            if (mode == "frame")
                break;
            if (mode == "rally" || mode == "soak") {
                if (!neutral)
                    neutral = image.width == 320 && image.height == 180 && image.corner == 0xff352316;
                else if (!pressed)
                    pressed = image.corner == 0xff694c19;
                else if (!released)
                    released = image.corner == 0xff352316;
            }
        }
    } while (stats.state == SC_RUNNING &&
             std::chrono::steady_clock::now() - start < std::chrono::seconds(mode == "soak" ? 300 : 60));
    const double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
    std::cout << "title=" << sc_title(s.get()) << " instructions=" << stats.instructions
              << " guest_bytes=" << stats.guest_bytes << " frames=" << last << " size=" << image.width << "x"
              << image.height << " elapsed_s=" << seconds << " input=" << pressed << " release=" << released
              << " audible_frames=" << audibleFrames << " pc=" << std::hex << stats.pc << std::dec
              << " status=" << sc_message(s.get()) << '\n'
              << sc_log(s.get()) << '\n';
    if (mode == "rally" || mode == "soak") {
        std::error_code ec;
        auto save = std::filesystem::path(argv[2]) / "sdmc" / "rally-record.bin";
        bool saved = std::filesystem::file_size(save, ec) == sizeof(uint32_t) && !ec;
        return !(released && audibleFrames > 0 && saved && stats.state == SC_EXITED &&
                 (mode != "soak" || last >= 6000));
    }
    return mode == "exit" ? stats.state != SC_EXITED : last == 0;
}
