// SPDX-License-Identifier: BSD-2-Clause-Patent
// SM8450 (waipio) UFS register facts and a read-only probe.
//
// Sources:
//   drivers/clk/qcom/gcc-sm8450.c            (clock branch and GDSC offsets)
//   arch/arm64/boot/dts/qcom/sm8450.dtsi     (UFS host 0x1d84000, PHY 0x1d87000)
//
// This module never writes hardware. It exists so the driver can map the
// correct GCC window and report the branch state before any clock is enabled.
// Do not reuse ShengUfs offsets: SM8550 uses 0x770xx and a separate
// UFS_MEM_PHY_GDSC at 0x9e000, neither of which exists here.
#ifndef SM8450_UFS_REGS_H
#define SM8450_UFS_REGS_H

#include <cstdint>

namespace sm8450_ufs {

constexpr uint64_t kGccBase = 0x00100000ULL;
constexpr uint32_t kGccUfsWindow = 0x87000;   // 4 KiB window holding every UFS branch
constexpr uint64_t kUfsHcBase = 0x01d84000ULL;
constexpr uint32_t kUfsHcSize = 0x3000;
constexpr uint64_t kUfsPhyBase = 0x01d87000ULL;  // SM8550 puts this at 0x1d80000
constexpr uint32_t kUfsPhySize = 0x1000;

// Single GDSC. There is no UFS_MEM_PHY_GDSC on this SoC.
constexpr uint32_t kUfsPhyGdscr = 0x87004;

constexpr uint32_t kBranchEnableBit = 0x1u;
constexpr uint32_t kClockOffBit = 0x1u << 31;  // hardware gating reports the branch off

struct Branch {
  const char* name;
  uint32_t offset;  // absolute GCC register offset
  bool hwcg;        // hwcg_bit = 1 in gcc-sm8450.c
};

// Absolute offsets from gcc-sm8450.c clk_branch entries.
constexpr Branch kBranches[] = {
    {"ufs_phy_axi", 0x87018, true},
    {"ufs_phy_ahb", 0x87020, true},
    {"ufs_phy_tx_symbol_0", 0x87024, false},
    {"ufs_phy_rx_symbol_0", 0x87028, false},
    {"ufs_phy_unipro_core", 0x87064, true},
    {"ufs_phy_ice_core", 0x8706c, true},
    {"ufs_phy_phy_aux", 0x870a4, true},
    {"ufs_phy_rx_symbol_1", 0x870c0, false},
    {"aggre_ufs_phy_axi", 0x870d4, true},
};

constexpr uint32_t kBranchCount = sizeof(kBranches) / sizeof(kBranches[0]);

// Contract: every branch lives in the one 4 KiB UFS GCC window.
constexpr bool windowOffsetValid(uint32_t absolute) {
  return absolute >= kGccUfsWindow && absolute < kGccUfsWindow + 0x1000;
}

constexpr uint32_t windowOffset(uint32_t absolute) { return absolute - kGccUfsWindow; }

constexpr bool branchEnabled(uint32_t value) { return (value & kBranchEnableBit) != 0; }
constexpr bool branchClockedOff(uint32_t value) { return (value & kClockOffBit) != 0; }

// Read-only probe over a mapped GCC window. `read` returns the 32-bit value at
// a window offset; `ctx` is the caller's mapping handle.
using ReadFn = uint32_t (*)(void* ctx, uint32_t offset);

struct BranchState {
  const char* name;
  uint32_t offset;
  uint32_t value;
  bool enabled;
  bool gated_off;
};

struct Probe {
  bool gdsc_enabled;
  uint32_t gdsc;
  BranchState branches[kBranchCount];
  bool all_enabled;
  bool any_gated_off;
};

inline Probe probe(ReadFn read, void* ctx) {
  Probe p{};
  p.gdsc = read(ctx, windowOffset(kUfsPhyGdscr));
  p.gdsc_enabled = (p.gdsc & 0x1u) != 0;
  p.all_enabled = true;
  p.any_gated_off = false;
  for (uint32_t i = 0; i < kBranchCount; ++i) {
    const uint32_t off = windowOffset(kBranches[i].offset);
    const uint32_t value = read(ctx, off);
    p.branches[i] = {kBranches[i].name, kBranches[i].offset, value, branchEnabled(value),
                     branchClockedOff(value)};
    p.all_enabled = p.all_enabled && p.branches[i].enabled;
    p.any_gated_off = p.any_gated_off || p.branches[i].gated_off;
  }
  return p;
}

}  // namespace sm8450_ufs

#endif
