// SPDX-License-Identifier: GPL-3.0-only
#include "SwitchCore.h"
#include "backends/switch1/Profile.hpp"
#include "backends/switch2-experimental/Research.hpp"
#include "core/system.hpp"
#include <cstring>
#include <fstream>
#include <stdexcept>

using NeXo2::Core::System;
namespace fs = std::filesystem;
static_assert(NeXo2::Core::Interpreter::TICK_FREQUENCY == switchcore::switch1::counter_hz);
static_assert(NeXo2::HLE::NUM_CORES == switchcore::switch1::cores);

struct SCSession {
    std::unique_ptr<System> system;
    SCStats stats{};
    std::string message;
    std::string title;
    void fail(const std::string& reason) {
        stats.state = SC_FAULT;
        message = reason;
    }
};

SCSession *sc_create() { return new (std::nothrow) SCSession; }
void sc_destroy(SCSession *s) { delete s; }

bool sc_load(SCSession *s, SCBackend backend, const char *file, const char *directory) {
    if (!s) return false;
    *s = SCSession{};
    try {
        if (backend != SC_SWITCH_1) throw std::runtime_error(switchcore::switch2::status);
        if (!file || !directory || !*directory) throw std::runtime_error("Percorso mancante.");
        const auto path = fs::u8path(file);
        std::ifstream stream(path, std::ios::binary | std::ios::ate);
        if (!stream) throw std::runtime_error("Impossibile aprire la NRO.");
        const auto length = stream.tellg();
        if (length < 0x80 || uint64_t(length) > switchcore::switch1::max_nro)
            throw std::runtime_error("Dimensione NRO fuori dai limiti (128 MiB).");
        std::vector<u8> data(static_cast<size_t>(length));
        stream.seekg(0);
        if (!stream.read(reinterpret_cast<char*>(data.data()), length))
            throw std::runtime_error("Lettura NRO incompleta.");
        auto root = fs::u8path(directory) / "sdmc";
        fs::create_directories(root);
        s->system = std::make_unique<System>();
        s->system->GetKernel().SetSdmcRoot(root);
        const auto name = path.filename().u8string();
        if (!s->system->LoadNro(data, std::string(name.begin(), name.end())))
            throw std::runtime_error(s->system->GetLastError());
        s->title = s->system->GetNroInfo()->title;
        s->stats.state = SC_RUNNING;
        return true;
    } catch (const std::exception& e) {
        s->system.reset();
        s->fail(e.what());
        return false;
    }
}
void sc_cache(SCSession *s, bool enabled) {
    if (s && s->system) s->system->GetCpu().SetDecodeCacheEnabled(enabled);
}
void sc_pad(SCSession *s, uint64_t buttons, int32_t lx, int32_t ly, int32_t rx, int32_t ry) {
    if (s && s->stats.state == SC_RUNNING)
        s->system->GetKernel().SetPadInput({buttons, lx, ly, rx, ry});
}
void sc_touch(SCSession *s, bool down, uint32_t x, uint32_t y) {
    if (!s || s->stats.state != SC_RUNNING) return;
    std::vector<NeXo2::HLE::TouchPoint> points;
    if (down) points.push_back({std::min(x, 1279u), std::min(y, 719u), 0});
    s->system->GetKernel().SetTouchInput(points);
}
void sc_audio(SCSession *s, SCAudioSink sink, void *context) {
    if (s && s->system) s->system->GetKernel().SetAudioSink([=](const s16 *pcm, size_t frames) {
        if (sink) sink(context, pcm, frames);
    });
}
SCStats sc_step(SCSession *s, uint32_t budget) {
    if (!s) return {};
    if (s->stats.state != SC_RUNNING) return s->stats;
    try {
        auto& sys = *s->system;
        s->stats.instructions += sys.Run(std::min(budget, 250000u));
        s->stats.pc = sys.GetCpu().GetState().pc;
        s->stats.guest_bytes = sys.GetMemory().AllocatedBytes();
        auto& kernel = sys.GetKernel();
        if (kernel.HasExited()) {
            s->stats.state = SC_EXITED;
            s->message = "Processo terminato.";
        } else if (sys.GetCpu().IsHalted()) s->fail(sys.GetCpu().GetHaltReason());
        else if (s->stats.guest_bytes > (512ull << 20)) s->fail("Budget di memoria guest superato.");
        auto guard = kernel.GetDisplay().LockFrame();
        s->stats.frames = kernel.GetDisplay().Frame().count;
    } catch (const std::exception& e) { s->fail(e.what()); }
    return s->stats;
}
bool sc_frame(SCSession *s, uint64_t after, SCFrameSink sink, void *context, uint64_t *number) {
    if (!s || !s->system || !sink || !number) return false;
    auto& display = s->system->GetKernel().GetDisplay();
    auto guard = display.LockFrame();
    const auto& frame = display.Frame();
    if (frame.count <= after || !frame.width || !frame.height ||
        frame.rgba.size() != uint64_t(frame.width) * frame.height) return false;
    sink(context, frame.rgba.data(), frame.width, frame.height);
    *number = frame.count;
    return true;
}
const char *sc_message(SCSession *s) { return s ? s->message.c_str() : "Sessione assente."; }
const char *sc_title(SCSession *s) { return s ? s->title.c_str() : ""; }
const char *sc_log(SCSession *s) {
    return s && s->system ? s->system->GetKernel().GetDebugOutput().c_str() : "";
}

