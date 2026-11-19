// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "arm64/jit_dynarmic.hpp"
#include <bitset>
#include <cstdint>
#include <mutex>
#include <string>

namespace switchcore {
// Code memory of the ARM64 recompiler (docs/nativo.md): one range below 64 GiB, reserved once, seen
// through an executable view and a writable alias. Each recompiler instance (one per emulated core)
// takes a slot and returns it when destroyed. iOS 26 prepares executable memory only while the
// debugger is attached at startup, so the pool never grows and later sessions reuse its slots.
class CodePool final : public NeXo2::Core::JitCodeMemory {
  public:
    static constexpr size_t slots = 4;
    static constexpr size_t slot_bytes = size_t(64) << 20;
    // Reserves the range on the first call. False, with the reason, where the host cannot.
    bool reserve(std::string &error);
    NeXo2::Core::JitCodeRegion Acquire() override;
    void Release(const NeXo2::Core::JitCodeRegion &region) override;

  private:
    std::mutex lock;
    uintptr_t execute = 0, write = 0;
    std::bitset<slots> used;
};
CodePool &code_pool();
} // namespace switchcore
