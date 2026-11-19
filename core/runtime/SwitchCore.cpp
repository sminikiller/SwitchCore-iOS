// SPDX-License-Identifier: GPL-3.0-only
#include "SwitchCore.h"
#include "SdPath.hpp"
#include "backends/switch1/Profile.hpp"
#include "backends/switch2-experimental/Research.hpp"
#include "core/system.hpp"
#ifdef NEXO2_HAS_VULKAN
#include "video_core/vulkan/vk_rasterizer.hpp"
#endif
#ifdef NEXO2_HAS_JIT
#include "CodePool.hpp"
#endif
#include <algorithm>
#include <filesystem>
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
    fs::path menu;           // NRO opened by sc_load; programs it launches return to it
    bool launched = false;   // the running program was chosen through envSetNextLoad
    uint64_t frame_base = 0; // frames presented by earlier programs of this session
    bool jit = false;        // recompiler chosen with sc_set_jit, kept by sc_load
    void fail(const std::string &reason) {
        stats.state = SC_FAULT;
        message = reason;
    }
};

namespace {
// Imported files and files chosen by the guest share the same size limit.
void start(SCSession &s, const fs::path &path, const std::string &argv) {
    std::error_code error;
    const auto length = fs::file_size(path, error);
    if (error)
        throw std::runtime_error("Impossibile aprire la NRO.");
    if (length < 0x80 || length > switchcore::switch1::max_nro)
        throw std::runtime_error("Dimensione NRO fuori dai limiti (128 MiB).");
    const auto name = path.u8string();
    if (!s.system->LoadNroFile(std::string(name.begin(), name.end()), argv))
        throw std::runtime_error(s.system->GetLastError());
    s.title = s.system->GetNroInfo()->title;
}

// Homebrew loader contract: a menu stores the chosen NRO with envSetNextLoad and exits. A program it
// launched returns to the menu when it ends without choosing another one.
bool resume(SCSession &s) {
    if (s.menu.empty())
        return false;
    auto &kernel = s.system->GetKernel();
    const std::string next = kernel.GetNextLoadPath();
    if (next.empty() && !s.launched)
        return false;
    fs::path target = s.menu;
    std::string argv;
    if (!next.empty()) {
        const auto file = switchcore::sd_file(kernel.GetSdmcRoot(), next);
        if (!file)
            throw std::runtime_error("NRO richiesta fuori dalla SD del titolo.");
        target = *file;
        argv = kernel.GetNextLoadArgv();
    }
    {
        auto guard = kernel.GetDisplay().LockFrame();
        s.frame_base += kernel.GetDisplay().Frame().count;
    }
    start(s, target, argv);
    s.launched = !next.empty();
    return true;
}

// The recompiler takes its code cache from the fixed pool that iOS needs (docs/nativo.md). Where the
// pool cannot be reserved, Dynarmic keeps allocating its own memory.
void apply_jit(SCSession &s) {
#ifdef NEXO2_HAS_JIT
    std::string error;
    if (s.jit && switchcore::code_pool().reserve(error))
        NeXo2::Core::JitBackend::SetCodeMemory(&switchcore::code_pool());
#endif
    s.system->GetCpu().SetJitEnabled(s.jit);
}
} // namespace

SCSession *sc_create() { return new (std::nothrow) SCSession; }
void sc_destroy(SCSession *s) { delete s; }

bool sc_load(SCSession *s, SCBackend backend, const char *file, const char *directory) {
    if (!s)
        return false;
    const bool jit = s->jit;
    *s = SCSession{};
    s->jit = jit;
    try {
        if (backend != SC_SWITCH_1)
            throw std::runtime_error(switchcore::switch2::status);
        if (!file || !directory || !*directory)
            throw std::runtime_error("Percorso mancante.");
        auto root = fs::u8path(directory) / "sdmc";
        fs::create_directories(root);
        s->system = std::make_unique<System>();
        s->system->GetKernel().SetSdmcRoot(root);
        const auto path = fs::u8path(file);
        if (fs::is_directory(path)) {
            if (!s->system->LoadGame(file))
                throw std::runtime_error(s->system->GetLastError());
            s->title = s->system->GetProgramName();
        } else {
            s->menu = path;
            start(*s, path, "");
        }
        apply_jit(*s);
        s->system->GetMemory().EnableGuestProtection();
        s->stats.state = SC_RUNNING;
        return true;
    } catch (const std::exception &e) {
        s->system.reset();
        s->fail(e.what());
        return false;
    }
}
bool sc_set_gpu(SCSession *s, bool accelerated) {
    if (!s || !s->system)
        return false;
    auto &gpu = s->system->GetKernel().GetGpu();
    using Kind = NeXo2::GPU::RendererKind;
    return gpu.SetRenderer(accelerated ? Kind::Vulkan : Kind::Software, &s->message);
}
void sc_cache(SCSession *s, bool enabled) {
    if (s && s->system)
        s->system->GetCpu().SetDecodeCacheEnabled(enabled);
}
bool sc_set_jit(SCSession *s, bool enabled) {
    if (!s)
        return false;
    s->jit = enabled && NeXo2::Core::Interpreter::JitAvailable();
    if (s->system)
        apply_jit(*s);
    return s->jit;
}
SCJitStats sc_jit_stats(SCSession *s) {
    SCJitStats stats{};
    if (!s || !s->system || !s->system->GetCpu().IsJitEnabled())
        return stats;
    stats.active = true;
    if (const auto *jit = s->system->GetCpu().GetJit()) {
        const auto &counters = jit->GetStats();
        stats.code_base = counters.code_base;
        stats.interpreted_words = counters.interpreted_words;
        stats.fallbacks = counters.fallbacks;
    }
    return stats;
}
void sc_pad(SCSession *s, uint64_t buttons, int32_t lx, int32_t ly, int32_t rx, int32_t ry) {
    if (s && s->stats.state == SC_RUNNING)
        s->system->GetKernel().SetPadInput({buttons, lx, ly, rx, ry});
}
void sc_touch(SCSession *s, bool down, uint32_t x, uint32_t y) {
    if (!s || s->stats.state != SC_RUNNING)
        return;
    std::vector<NeXo2::HLE::TouchPoint> points;
    if (down)
        points.push_back({std::min(x, 1279u), std::min(y, 719u), 0});
    s->system->GetKernel().SetTouchInput(points);
}
void sc_audio(SCSession *s, SCAudioSink sink, void *context) {
    if (s && s->system)
        s->system->GetKernel().SetAudioSink([=](const s16 *pcm, size_t frames) {
            if (sink)
                sink(context, pcm, frames);
        });
}
SCStats sc_step(SCSession *s, uint32_t budget) {
    if (!s)
        return {};
    if (s->stats.state != SC_RUNNING)
        return s->stats;
    try {
        auto &sys = *s->system;
        s->stats.instructions += sys.Run(std::min(budget, 250000u));
        s->stats.pc = sys.GetCpu().GetState().pc;
        s->stats.guest_bytes = sys.GetMemory().AllocatedBytes();
        auto &kernel = sys.GetKernel();
#ifdef NEXO2_HAS_VULKAN
        if (auto *hw = kernel.GetGpu().Hw())
            s->stats.gpu_draws = hw->GetStats().draws;
#endif
        if (kernel.HasExited()) {
            if (!resume(*s)) {
                s->stats.state = SC_EXITED;
                s->message = "Processo terminato.";
            }
        } else if (sys.GetCpu().IsHalted())
            s->fail(sys.GetCpu().GetHaltReason());
        else if (s->stats.guest_bytes > (512ull << 20))
            s->fail("Budget di memoria guest superato.");
        auto guard = kernel.GetDisplay().LockFrame();
        s->stats.frames = s->frame_base + kernel.GetDisplay().Frame().count;
    } catch (const std::exception &e) {
        s->stats.pc = s->system->GetCpu().GetState().pc;
        s->fail(e.what());
    }
    return s->stats;
}
bool sc_frame(SCSession *s, uint64_t after, SCFrameSink sink, void *context, uint64_t *number) {
    if (!s || !s->system || !sink || !number)
        return false;
    auto &display = s->system->GetKernel().GetDisplay();
    auto guard = display.LockFrame();
    const auto &frame = display.Frame();
    const uint64_t count = s->frame_base + frame.count;
    if (!frame.count || count <= after || !frame.width || !frame.height ||
        frame.rgba.size() != uint64_t(frame.width) * frame.height)
        return false;
    sink(context, frame.rgba.data(), frame.width, frame.height);
    *number = count;
    return true;
}
const char *sc_message(SCSession *s) { return s ? s->message.c_str() : "Sessione assente."; }
const char *sc_title(SCSession *s) { return s ? s->title.c_str() : ""; }
const char *sc_log(SCSession *s) {
    return s && s->system ? s->system->GetKernel().GetDebugOutput().c_str() : "";
}
