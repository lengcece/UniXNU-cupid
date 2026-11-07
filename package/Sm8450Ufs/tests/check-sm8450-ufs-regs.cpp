// SPDX-License-Identifier: BSD-2-Clause-Patent
// Host check for the SM8450 UFS register table. No hardware, no IOKit.
#include "../Sm8450UfsRegs.h"

#include <cstdio>
#include <cstring>

namespace {
int failures = 0;

void check(bool ok, const char* what) {
  if (!ok) {
    std::printf("FAIL %s\n", what);
    ++failures;
  }
}

uint32_t fake_read(void* ctx, uint32_t offset) {
  const uint32_t* window = static_cast<const uint32_t*>(ctx);
  return window[offset / 4];
}
}  // namespace

int main() {
  using namespace sm8450_ufs;

  // The documented SM8450 values.
  const uint32_t expected[] = {0x87018, 0x87020, 0x87024, 0x87028, 0x87064,
                               0x8706c, 0x870a4, 0x870c0, 0x870d4};
  check(kBranchCount == 9, "nine UFS branches");
  for (uint32_t i = 0; i < kBranchCount; ++i) {
    check(kBranches[i].offset == expected[i], kBranches[i].name);
    check(windowOffsetValid(kBranches[i].offset), "branch inside the 0x870xx window");
    check(kBranches[i].offset < 0x88000, "branch below the next GCC block");
    check(kBranches[i].offset >= 0x87000, "branch not in the SM8550 0x77xxx block");
  }
  check(kUfsPhyGdscr == 0x87004, "GDSC at 0x87004");
  check(kUfsHcBase == 0x01d84000, "UFS host base");
  check(kUfsPhyBase == 0x01d87000, "UFS PHY base (not SM8550 0x1d80000)");
  check(windowOffset(0x87018) == 0x18, "window-relative offset");

  // No SM8550-only values leaked in.
  for (uint32_t i = 0; i < kBranchCount; ++i) {
    check((kBranches[i].offset & ~0xfffu) != 0x77000, "no SM8550 branch");
  }
  check(kUfsPhyGdscr != 0x9e000, "no UFS_MEM_PHY_GDSC offset");

  // Probe against a fake window: every branch enabled, hardware-gated off.
  uint32_t window[0x1000 / 4] = {};
  window[windowOffset(kUfsPhyGdscr) / 4] = 0x1;
  for (uint32_t i = 0; i < kBranchCount; ++i) {
    window[windowOffset(kBranches[i].offset) / 4] = kBranchEnableBit | kClockOffBit;
  }
  const Probe p = probe(fake_read, window);
  check(p.gdsc_enabled, "gdsc enabled");
  check(p.all_enabled, "all branches enabled");
  check(p.any_gated_off, "gated-off branch reported (CLK_OFF with enable)");
  check(p.branches[0].value == (kBranchEnableBit | kClockOffBit), "raw value kept");
  check(branchEnabled(0) == false, "disabled branch");
  check(branchEnabled(1) == true, "enabled branch");

  if (failures) {
    std::printf("%d failures\n", failures);
    return 1;
  }
  std::printf("SM8450 UFS register table: %u branches, all checks passed\n", kBranchCount);
  return 0;
}
