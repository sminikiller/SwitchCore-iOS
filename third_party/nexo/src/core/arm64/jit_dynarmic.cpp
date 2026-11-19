// JIT con dynarmic: ver jit_dynarmic.hpp y docs/07-nexo-internals/jit.md.
//
// Como encaja con el resto:
//   Interpreter::Run()  --(JIT activado)-->  JitBackend::Run()
//     1. Copia los registros de Interpreter::GetState() al JIT.
//     2. dynarmic traduce los bloques que haga falta (pidiendo las instrucciones con
//        MemoryReadCode) y los ejecuta en el PC.
//     3. Copia los registros de vuelta.
//   Mientras el codigo traducido corre, dynarmic nos llama (UserCallbacks) para:
//     - memoria que no esta en la tabla de paginas (o paginas con codigo traducido),
//     - SVC (el kernel HLE: se le pasa el estado exactamente como con el interprete),
//     - instrucciones que dynarmic no sabe hacer: las hace nuestro interprete,
//     - BRK y demas excepciones, y contar el tiempo (ticks = instrucciones).
//
// SwitchCore: the ARM64 backend of dynarmic has no interpreter fallback and stops on a few IR
// operations. MemoryReadCode checks every word first and hides the ones the backend cannot emit;
// dynarmic then raises NoExecuteFault and ExceptionRaised runs that word with the interpreter.
#include "jit_dynarmic.hpp"
#include "interpreter.hpp"
#include "fp_ops.hpp"
#include "common/logger.hpp"
#include <atomic>
#include <cstdio>
#include <cstdlib>

#ifdef NEXO2_HAS_JIT
#include <array>
#include <exception>
#include <iterator>
#include <optional>
#include <stdexcept>
#include <utility>
#include <dynarmic/interface/A64/a64.h>
#include <dynarmic/interface/A64/config.h>
#include <dynarmic/interface/exclusive_monitor.h>
#include <dynarmic/interface/optimization_flags.h>
// SwitchCore: dynarmic internals used to check words before translation (JitImpl::CanEmit)
#include <dynarmic/frontend/A64/a64_location_descriptor.h>
#include <dynarmic/frontend/A64/translate/a64_translate.h>
#include <dynarmic/ir/basic_block.h>
#include <dynarmic/ir/opcodes.h>
#if defined(__aarch64__) || defined(_M_ARM64)
#include <dynarmic/backend/arm64/abi.h>
#endif
#endif

namespace NeXo2::Core {

using NeXo2::Common::Logger;

namespace {
std::atomic<JitCodeMemory*> g_codeMemory{nullptr};   // SwitchCore: JitBackend::SetCodeMemory
}

#ifdef NEXO2_HAS_JIT

#if defined(__aarch64__) || defined(_M_ARM64)
constexpr bool kArm64Host = true;
// SwitchCore: Darwin changes the host x18 on every context switch. The backend must neither allocate
// nor save it; the guest x18 lives in the JIT state like every other guest register.
namespace ArmAbi = Dynarmic::Backend::Arm64;
static_assert([] {
    for (int reg : ArmAbi::GPR_ORDER)
        if (reg == 18) return false;
    return true;
}());
static_assert(((ArmAbi::ABI_CALLER_SAVE | ArmAbi::ABI_CALLEE_SAVE) & (u64(1) << 18)) == 0);
static_assert(ArmAbi::Xstate.index() != 18 && ArmAbi::Xhalt.index() != 18 && ArmAbi::Xticks.index() != 18 &&
              ArmAbi::Xfastmem.index() != 18 && ArmAbi::Xpagetable.index() != 18 &&
              ArmAbi::Xscratch0.index() != 18 && ArmAbi::Xscratch1.index() != 18 &&
              ArmAbi::Xscratch2.index() != 18);
#else
constexpr bool kArm64Host = false;
#endif

// SwitchCore: IR operations whose ARM64 emitter is an ASSERT_FALSE("Unimplemented") stub at the
// pinned dynarmic commit (backend/arm64/emit_arm64_*.cpp). Recheck when updating dynarmic.
// FPVectorRoundInt16 (FRINT* on half-precision vectors) is emitted but wrong: its software fallback
// restores the result register after the call (ABI_CALLER_SAVE & ~(1 << Q index) clears a GPR bit),
// which the CPU fuzz test reports as incorrect results.
bool Arm64CannotEmit(Dynarmic::IR::Opcode op) {
    using O = Dynarmic::IR::Opcode;
    switch (op) {
    case O::FPVectorRoundInt16:
    case O::SM4AccessSubstitutionBox:
    case O::FPAbs16:
    case O::FPMulAdd16:
    case O::FPMulSub16:
    case O::FPNeg16:
    case O::FPRecipEstimate16:
    case O::FPRecipExponent16:
    case O::FPRecipStepFused16:
    case O::FPRoundInt16:
    case O::FPRSqrtEstimate16:
    case O::FPRSqrtStepFused16:
    case O::FPHalfToFixedS16:
    case O::FPHalfToFixedS32:
    case O::FPHalfToFixedS64:
    case O::FPHalfToFixedU16:
    case O::FPHalfToFixedU32:
    case O::FPHalfToFixedU64:
    case O::SignedSaturatedAdd8:
    case O::SignedSaturatedAdd16:
    case O::SignedSaturatedAdd32:
    case O::SignedSaturatedAdd64:
    case O::SignedSaturatedDoublingMultiplyReturnHigh16:
    case O::SignedSaturatedDoublingMultiplyReturnHigh32:
    case O::SignedSaturatedSub8:
    case O::SignedSaturatedSub16:
    case O::SignedSaturatedSub32:
    case O::SignedSaturatedSub64:
    case O::UnsignedSaturatedAdd8:
    case O::UnsignedSaturatedAdd16:
    case O::UnsignedSaturatedAdd32:
    case O::UnsignedSaturatedAdd64:
    case O::UnsignedSaturatedSub8:
    case O::UnsignedSaturatedSub16:
    case O::UnsignedSaturatedSub32:
    case O::UnsignedSaturatedSub64:
    case O::VectorEqual128:
    case O::VectorMaxS64:
    case O::VectorMaxU64:
    case O::VectorMinS64:
    case O::VectorMinU64:
    case O::VectorSignExtend64:
    case O::VectorSignedMultiply16:
    case O::VectorSignedMultiply32:
    case O::VectorUnsignedMultiply16:
    case O::VectorUnsignedMultiply32:
    case O::FPVectorEqual16:
    case O::FPVectorMulAdd16:
    case O::FPVectorNeg16:
    case O::FPVectorRecipEstimate16:
    case O::FPVectorRecipStepFused16:
    case O::FPVectorRSqrtEstimate16:
    case O::FPVectorRSqrtStepFused16:
    case O::FPVectorToSignedFixed16:
    case O::FPVectorToUnsignedFixed16:
        return true;
    default:
        return false;
    }
}

// Monitor exclusivo comun a todos los nucleos (LDXR en uno, STXR en otro...).
// Dynarmic lo usa para que solo un nucleo a la vez haga su "escritura exclusiva".
Dynarmic::ExclusiveMonitor& SharedMonitor() {
    static Dynarmic::ExclusiveMonitor monitor(8);
    return monitor;
}

class JitImpl final : public Dynarmic::A64::UserCallbacks {
public:
    using VAddr  = Dynarmic::A64::VAddr;
    using Vector = Dynarmic::A64::Vector;

    JitImpl(Interpreter& cpu, Memory& memory)
        : m_cpu(cpu), m_mem(memory), m_monitor(SharedMonitor()), m_core(cpu.CoreIndex() % 8) {
        Dynarmic::A64::UserConfig conf{};
        conf.callbacks = this;
        conf.processor_id = m_core;
        conf.global_monitor = &m_monitor;
        // Registros de sistema que el codigo traducido lee directamente de nuestro CPUState
        conf.tpidrro_el0 = &cpu.m_state.tpidrro_el0;
        conf.tpidr_el0 = &cpu.m_state.tpidr_el0;
        conf.cntfrq_el0 = u32(Interpreter::TICK_FREQUENCY);
        conf.ctr_el0 = 0x8444C004;   // lineas de cache de 64 bytes (como el interprete)
        conf.dczid_el0 = 4;          // DC ZVA borra 64 bytes
        // Tabla de paginas: acceso directo a la memoria emulada sin llamar a funciones
        conf.page_table = m_mem.EnableFlatPageTable();
        conf.page_table_address_space_bits = Memory::FLAT_BITS;
        conf.silently_mirror_page_table = false;   // fuera de la tabla -> callbacks (leen 0)
        // Un acceso que cruza el borde de una pagina iria a otra pagina del PC que no
        // tiene nada que ver: esos van por los callbacks.
        conf.detect_misaligned_access_via_page_table = 16 | 32 | 64 | 128;
        conf.only_detect_misalignment_via_page_table_on_page_boundary = true;
        conf.define_unpredictable_behaviour = true;
        conf.enable_cycle_counting = true;
        conf.code_cache_size = 128 * 1024 * 1024;
        // SwitchCore: on ARM64 hosts the code cache can come from a provider, such as a fixed pool
        // with a writable alias. It replaces dynarmic's own allocation for this instance.
        if constexpr (kArm64Host) {
            if (JitCodeMemory* provider = g_codeMemory.load(std::memory_order_acquire)) {
                const JitCodeRegion region = provider->Acquire();
                if (region.size) {
                    m_provider = provider;
                    m_region = region;
                    conf.code_cache_execute = region.execute;
                    conf.code_cache_write = region.write;
                    conf.code_cache_size = region.size;
                    stats.code_base = reinterpret_cast<u64>(region.execute);
                }
            }
        }
        // Sin el paso que junta instrucciones desconocidas seguidas en un solo "hazlo tu"
        // (InterpreterFallback de N): se pasaria del limite de instrucciones y de Step().
        conf.optimizations = Dynarmic::all_safe_optimizations & ~Dynarmic::OptimizationFlag::MiscIROpt;
        try {
            m_jit = std::make_unique<Dynarmic::A64::Jit>(conf);
        } catch (...) {
            if (m_provider) m_provider->Release(m_region);
            throw;
        }
        m_generation = m_mem.Generation();
        m_seenCodeWrites = m_mem.CodeWriteCount();
    }

    ~JitImpl() override {
        m_jit.reset();   // SwitchCore: the translated code lives in m_region until here
        if (m_provider) m_provider->Release(m_region);
    }

    // ------------------------------------------------------------------
    // Ejecutar
    // ------------------------------------------------------------------
    u64 Run(u64 max_steps) {
        ++stats.runs;
        if (m_generation != m_mem.Generation()) {   // otro programa cargado
            m_generation = m_mem.Generation();
            m_seenCodeWrites = m_mem.CodeWriteCount();
            m_jit->ClearCache();
        }
        CheckCodeWrites();                          // el HLE pudo escribir en codigo

        const u64 start = m_cpu.m_instructionCount;
        m_end = start + max_steps;
        m_stepFallback = -1;
        LoadIntoJit();
        m_running = true;
        if (max_steps == 1) m_jit->Step();          // una sola instruccion (tests, Step de la UI)
        else                m_jit->Run();
        m_running = false;
        m_jit->ClearHalt(Dynarmic::HaltReason::UserDefined1);
        SaveFromJit();
        if (max_steps == 1) {
            // Una instruccion: cuenta como el interprete (0 si no existe y paro la CPU)
            m_cpu.m_instructionCount = start + (m_stepFallback >= 0 ? u64(m_stepFallback) : 1);
        }
        // Los flags de la FPU del PC son del emulador, no del programa (el JIT lleva los suyos)
        FP::ClearHostFlags();
        // SwitchCore: C++ exceptions cannot unwind through translated code. One raised by the
        // interpreter or the kernel inside a callback was kept, and leaves now with the state saved.
        if (m_pending) std::rethrow_exception(std::exchange(m_pending, nullptr));
        return m_cpu.m_instructionCount - start;
    }

    void RequestHalt() {
        if (m_running) m_jit->HaltExecution(Dynarmic::HaltReason::UserDefined1);
    }
    void ClearExclusive() {
        m_monitor.ClearProcessor(m_core);
        m_jit->ClearExclusiveState();
    }
    void ClearCache() { m_jit->ClearCache(); }

    JitBackend::Stats stats;

    // ------------------------------------------------------------------
    // Estado: CPUState <-> registros del JIT
    // ------------------------------------------------------------------
    void LoadIntoJit() {
        CPUState& s = m_cpu.m_state;
        std::array<u64, 31> regs;
        for (size_t i = 0; i < 31; ++i) regs[i] = s.x[i];
        m_jit->SetRegisters(regs);
        m_jit->SetSP(s.sp);
        m_jit->SetPC(s.pc);
        m_jit->SetPstate(u32(s.GetNZCV()));
        std::array<Vector, 32> vecs;
        for (size_t i = 0; i < 32; ++i) vecs[i] = {s.v[i].lo, s.v[i].hi};
        m_jit->SetVectors(vecs);
        m_jit->SetFpcr(u32(s.fpcr));
        m_jit->SetFpsr(u32(s.fpsr));
    }
    void SaveFromJit() {
        CPUState& s = m_cpu.m_state;
        const auto regs = m_jit->GetRegisters();
        for (size_t i = 0; i < 31; ++i) s.x[i] = regs[i];
        s.x[31] = 0;
        s.sp = m_jit->GetSP();
        s.pc = m_jit->GetPC();
        s.SetNZCV(m_jit->GetPstate());
        const auto vecs = m_jit->GetVectors();
        for (size_t i = 0; i < 32; ++i) { s.v[i].lo = vecs[i][0]; s.v[i].hi = vecs[i][1]; }
        s.fpcr = m_jit->GetFpcr();
        s.fpsr = m_jit->GetFpsr();
    }

    // Tras algo que pudo parar la CPU o pedir cambio de hilo: que el JIT salga
    void HaltIfNeeded() {
        if (m_cpu.m_halted || m_cpu.m_stopRequested || m_pending)
            m_jit->HaltExecution(Dynarmic::HaltReason::UserDefined1);
    }

    // Ejecuta 'count' instrucciones con nuestro interprete, desde el PC actual del JIT
    void RunInterpreter(VAddr pc, u64 count) {
        SaveFromJit();
        m_cpu.m_state.pc = pc;
        FP::ClearHostFlags();
        const u64 ticks = m_cpu.m_instructionCount;
        u64 done = 0;
        try {
            done = m_cpu.RunPlain(count);
        } catch (...) {
            m_pending = std::current_exception();    // SwitchCore: Run() throws it (MMU fault...)
        }
        m_cpu.m_instructionCount = ticks;            // ya las cuenta dynarmic (son parte del bloque)
        m_stepFallback = s64(done);
        FP::FoldHostFlags(m_cpu.m_state.fpsr);       // flags de la coma flotante del interprete
        LoadIntoJit();
        HaltIfNeeded();
    }

    // El programa (o el HLE) escribio en paginas con codigo traducido: tirar esos bloques
    void CheckCodeWrites() {
        if (m_mem.CodeWriteCount() == m_seenCodeWrites) return;
        std::array<VAddr, 8> pages;
        size_t count = 0;
        if (!m_mem.TakeCodeWrites(m_seenCodeWrites, pages, count)) {
            m_jit->ClearCache();
            stats.invalidations += 8;
            return;
        }
        for (size_t i = 0; i < count; ++i) m_jit->InvalidateCacheRange(pages[i], Memory::PAGE_SIZE);
        stats.invalidations += count;
    }

    // SwitchCore: translates one word on its own and reports whether the ARM64 backend can emit
    // it: no IR::Term::Interpret and no operation from Arm64CannotEmit. One IR block serves many
    // words, because creating a block allocates a pool for 4096 instructions.
    bool CanEmit(VAddr pc, u32 word) {
        namespace IR = Dynarmic::IR;
        if (!m_probe || ++m_probeWords > 256) {
            m_probe.emplace(Dynarmic::A64::LocationDescriptor{pc, Dynarmic::FP::FPCR{}, false});
            m_probeWords = 0;
        }
        IR::Block& block = *m_probe;
        auto& list = block.Instructions();
        const auto last = list.empty() ? list.end() : std::prev(list.end());
        const Dynarmic::A64::LocationDescriptor where{pc, Dynarmic::FP::FPCR{m_jit->GetFpcr()}, false};
        Dynarmic::A64::TranslateSingleInstruction(block, where, word);
        const IR::Terminal terminal = block.GetTerminal();
        bool ok = boost::get<IR::Term::Interpret>(&terminal) == nullptr;
        if (block.HasTerminal()) block.ReplaceTerminal(IR::Term::Invalid{});
        for (auto it = last == list.end() ? list.begin() : std::next(last); it != list.end(); ++it)
            if (Arm64CannotEmit(it->GetOpcode())) ok = false;
        return ok;
    }

    // ------------------------------------------------------------------
    // UserCallbacks: memoria
    // ------------------------------------------------------------------
    std::optional<u32> MemoryReadCode(VAddr vaddr) override {
        // Pagina que no existe: no hay codigo. (Si no, dynarmic, al juntar instrucciones
        // que no conoce, recorreria GB de ceros.) Ejecutar ahi -> NoExecuteFault -> el
        // interprete para la CPU con el mensaje de siempre.
        if (!m_mem.PagePointer(vaddr)) return std::nullopt;
        // SwitchCore: the same path enforces the guest execute permission, and takes the words
        // the backend cannot emit: the interpreter runs them or stops with its usual message.
        try {
            m_mem.RequireGuestAccess(vaddr, 4, 4);
        } catch (const std::runtime_error&) {
            return std::nullopt;
        }
        m_mem.MarkCode(vaddr);   // si luego se escribe aqui, nos enteramos (CheckCodeWrites)
        const u32 word = m_mem.Read<u32>(vaddr);
        if (!CanEmit(vaddr, word)) {
            ++stats.interpreted_words;
            return std::nullopt;
        }
        return word;
    }
    u8  MemoryRead8(VAddr a) override  { return m_mem.Read<u8>(a); }
    u16 MemoryRead16(VAddr a) override { return m_mem.Read<u16>(a); }
    u32 MemoryRead32(VAddr a) override { return m_mem.Read<u32>(a); }
    u64 MemoryRead64(VAddr a) override { return m_mem.Read<u64>(a); }
    Vector MemoryRead128(VAddr a) override { return {m_mem.Read<u64>(a), m_mem.Read<u64>(a + 8)}; }

    void MemoryWrite8(VAddr a, u8 v) override   { m_mem.Write<u8>(a, v);  CheckCodeWrites(); }
    void MemoryWrite16(VAddr a, u16 v) override { m_mem.Write<u16>(a, v); CheckCodeWrites(); }
    void MemoryWrite32(VAddr a, u32 v) override { m_mem.Write<u32>(a, v); CheckCodeWrites(); }
    void MemoryWrite64(VAddr a, u64 v) override { m_mem.Write<u64>(a, v); CheckCodeWrites(); }
    void MemoryWrite128(VAddr a, Vector v) override {
        m_mem.Write<u64>(a, v[0]);
        m_mem.Write<u64>(a + 8, v[1]);
        CheckCodeWrites();
    }

    // STXR: escribir solo si la memoria sigue teniendo lo que leyo LDXR (atomico de verdad:
    // otro nucleo puede estar escribiendo ahi a la vez)
    template <typename T>
    bool WriteExclusive(VAddr a, T value, T expected) {
        if (!m_mem.CompareExchange<T>(a, expected, value)) return false;
        CheckCodeWrites();
        return true;
    }
    bool MemoryWriteExclusive8(VAddr a, u8 v, u8 e) override    { return WriteExclusive(a, v, e); }
    bool MemoryWriteExclusive16(VAddr a, u16 v, u16 e) override { return WriteExclusive(a, v, e); }
    bool MemoryWriteExclusive32(VAddr a, u32 v, u32 e) override { return WriteExclusive(a, v, e); }
    bool MemoryWriteExclusive64(VAddr a, u64 v, u64 e) override { return WriteExclusive(a, v, e); }
    bool MemoryWriteExclusive128(VAddr a, Vector v, Vector e) override {
        u64 lo = e[0], hi = e[1];
        if (!m_mem.CompareExchange128(a, lo, hi, v[0], v[1])) return false;
        CheckCodeWrites();
        return true;
    }

    // ------------------------------------------------------------------
    // UserCallbacks: SVC, instrucciones que dynarmic no hace, excepciones
    // ------------------------------------------------------------------
    void CallSVC(u32 swi) override {
        ++stats.svc_calls;
        SaveFromJit();
        // dynarmic ya avanzo el PC; el kernel ve lo mismo que con el interprete (el PC del SVC)
        const u64 next_pc = m_cpu.m_state.pc;
        m_cpu.m_state.pc = next_pc - 4;
        try {
            if (m_cpu.m_svcHandler) m_cpu.m_svcHandler(swi, m_cpu.m_state);
        } catch (...) {
            m_pending = std::current_exception();   // SwitchCore: Run() throws it, PC on the SVC
        }
        m_cpu.m_state.x[31] = 0;
        if (!m_pending) m_cpu.m_state.pc = next_pc;
        LoadIntoJit();
        CheckCodeWrites();
        HaltIfNeeded();
    }

    void InterpreterFallback(VAddr pc, size_t num_instructions) override {
        stats.fallbacks += num_instructions;
        RunInterpreter(pc, num_instructions);
    }

    void ExceptionRaised(VAddr pc, Dynarmic::A64::Exception exception) override {
        using E = Dynarmic::A64::Exception;
        switch (exception) {
            case E::Breakpoint: {   // BRK: como el interprete, el PC se queda en el BRK
                SaveFromJit();
                m_cpu.m_state.pc = pc;
                char buf[48];
                std::snprintf(buf, sizeof(buf), "BRK #0x%X", (m_mem.Read<u32>(pc) >> 5) & 0xFFFF);
                m_cpu.Halt(buf);
                LoadIntoJit();
                m_jit->HaltExecution(Dynarmic::HaltReason::UserDefined1);
                return;
            }
            case E::WaitForInterrupt: case E::WaitForEvent: case E::SendEvent:
            case E::SendEventLocal: case E::Yield:
                return;   // pistas (hints): no hacen nada
            default:
                // Codificacion que dynarmic no conoce (PAC, alguna de ARMv8.2...) o reservada:
                // la hace nuestro interprete (o para la CPU con el mismo mensaje de siempre).
                stats.fallbacks += 1;
                RunInterpreter(pc, 1);
                return;
        }
    }

    // ------------------------------------------------------------------
    // UserCallbacks: tiempo. Para dynarmic un "tick" es una instruccion ejecutada; el
    // contador del sistema (CNTPCT) sale de las instrucciones con GetTicks().
    // ------------------------------------------------------------------
    void AddTicks(u64 ticks) override { m_cpu.m_instructionCount += ticks; }
    u64 GetTicksRemaining() override {
        return m_end > m_cpu.m_instructionCount ? m_end - m_cpu.m_instructionCount : 0;
    }
    u64 GetCNTPCT() override { return m_cpu.GetTicks(); }

private:
    Interpreter& m_cpu;
    Memory& m_mem;
    Dynarmic::ExclusiveMonitor& m_monitor;
    u32 m_core;
    std::unique_ptr<Dynarmic::A64::Jit> m_jit;
    u64 m_end = 0;
    u64 m_generation = 0;
    u64 m_seenCodeWrites = 0;
    std::atomic<bool> m_running{false};   // RequestHalt puede llegar desde otro hilo del PC
    s64 m_stepFallback = -1;   // instrucciones que hizo el interprete en un Step() (-1 = ninguna)
    // SwitchCore
    JitCodeMemory* m_provider = nullptr;   // owner of m_region, if the code cache came from it
    JitCodeRegion m_region;
    std::exception_ptr m_pending;          // exception from a callback, thrown by Run()
    std::optional<Dynarmic::IR::Block> m_probe;   // scratch IR for CanEmit
    unsigned m_probeWords = 0;
};

JitBackend::JitBackend(Interpreter& cpu, Memory& memory) : m_impl(std::make_unique<JitImpl>(cpu, memory)) {
    Logger::Log(Logger::Level::Info, kArm64Host ? "[JIT] dynarmic pronto (ARM64 -> ARM64)"
                                                : "[JIT] dynarmic listo (ARM64 -> x86-64)");
}
JitBackend::~JitBackend() = default;
u64  JitBackend::Run(u64 max_steps) { return m_impl->Run(max_steps); }
void JitBackend::RequestHalt() { m_impl->RequestHalt(); }
void JitBackend::ClearExclusive() { m_impl->ClearExclusive(); }
void JitBackend::ClearCache() { m_impl->ClearCache(); }
const JitBackend::Stats& JitBackend::GetStats() const { return m_impl->stats; }
bool JitBackend::Available() { return true; }

#else   // NeXo compilado sin JIT (NEXO2_ENABLE_JIT=OFF): todo sigue en el interprete

class JitImpl {};
JitBackend::JitBackend(Interpreter&, Memory&) {}
JitBackend::~JitBackend() = default;
u64  JitBackend::Run(u64) { return 0; }
void JitBackend::RequestHalt() {}
void JitBackend::ClearExclusive() {}
void JitBackend::ClearCache() {}
const JitBackend::Stats& JitBackend::GetStats() const { static Stats s; return s; }
bool JitBackend::Available() { return false; }

#endif

void JitBackend::SetCodeMemory(JitCodeMemory* provider) { g_codeMemory.store(provider, std::memory_order_release); }

} // namespace NeXo2::Core
