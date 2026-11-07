// SPDX-License-Identifier: BSD-2-Clause-Patent
// Host check for the SM8450 UFS register table and the UFSHCI command path.
// No hardware, no IOKit: a fake register file emulates controller readiness,
// UIC completion and the transfer doorbell.
#include "../Sm8450UfsHci.h"

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

// Minimal UFSHCI model: enough state transitions for Hci to complete.
class FakeRegs : public sm8450_ufs::RegOps {
 public:
  uint32_t regs[sm8450_ufs::kUfshciRegisterSpace / 4] = {};
  bool stuck = false;
  int rung = 0;

  uint32_t read32(uint32_t offset) override { return regs[offset / 4]; }

  void write32(uint32_t offset, uint32_t value) override {
    using namespace sm8450_ufs;
    switch (offset) {
      case kRegControllerEnable:
        if (!stuck) {
          if (value == kControllerEnable) {
            regs[kRegControllerStatus / 4] |= kStatusReady;
          } else {
            regs[kRegControllerStatus / 4] &= ~kStatusReady;
          }
        }
        return;
      case kRegUicCommand:
        if (!stuck) {
          regs[kRegInterruptStatus / 4] |= kIsUicCommandCompl;
          regs[kRegUicCommandArg2 / 4] = kUicResultSuccess;
        }
        return;
      case kRegUtpTransferReqDoorBell:
        ++rung;
        regs[kRegInterruptStatus / 4] |= kIsUtpTransferReqCompl;
        return;  // hardware clears the doorbell itself
      case kRegInterruptStatus:
        regs[kRegInterruptStatus / 4] &= ~value;  // write-one-to-clear
        return;
      default:
        regs[offset / 4] = value;
        return;
    }
  }

  void delayUs(uint32_t) override {}
};

uint32_t fake_window_read(void* ctx, uint32_t offset) {
  const uint32_t* window = static_cast<const uint32_t*>(ctx);
  return window[offset / 4];
}
}  // namespace

int main() {
  using namespace sm8450_ufs;

  // --- GCC register table (unchanged) -------------------------------------
  const uint32_t expected[] = {0x87018, 0x87020, 0x87024, 0x87028, 0x87064,
                               0x8706c, 0x870a4, 0x870c0, 0x870d4};
  check(kBranchCount == 9, "nine UFS branches");
  for (uint32_t i = 0; i < kBranchCount; ++i) {
    check(kBranches[i].offset == expected[i], kBranches[i].name);
    check(windowOffsetValid(kBranches[i].offset), "branch inside the 0x870xx window");
  }
  check(kUfsPhyGdscr == 0x87004, "GDSC at 0x87004");
  check(kUfsHcBase == 0x01d84000, "UFS host base");
  check(kUfsPhyBase == 0x01d87000, "UFS PHY base (not SM8550 0x1d80000)");
  check(kUfsPhyGdscr != 0x9e000, "no UFS_MEM_PHY_GDSC offset");

  uint32_t window[0x1000 / 4] = {};
  window[windowOffset(kUfsPhyGdscr) / 4] = 0x1;
  for (uint32_t i = 0; i < kBranchCount; ++i) {
    window[windowOffset(kBranches[i].offset) / 4] = kBranchEnableBit | kClockOffBit;
  }
  const Probe p = probe(fake_window_read, window);
  check(p.gdsc_enabled && p.all_enabled && p.any_gated_off, "probe state");

  // --- Device-tree match ---------------------------------------------------
  check(compatibleMatches("qcom,sm8450-ufshc"), "matches cupid compatible");
  check(!compatibleMatches("qcom,sm8550-ufshc"), "does not match SM8550");
  check(!compatibleMatches(nullptr), "null compatible rejected");

  // --- UFSHCI register map -------------------------------------------------
  check(kRegControllerEnable == 0x34, "HCE at 0x34 (UFSHCI 4.x)");
  check(kRegUicCommand == 0x90, "UIC command at 0x90");
  check(kRegUtpTransferReqDoorBell == 0x58, "doorbell at 0x58");
  check(kUfshciRegisterSpace == 0xA0, "register space 0xA0");

  // --- UTRD / UPIU layout --------------------------------------------------
  check(sizeof(Utrd) == 32, "UTRD 32 bytes");
  check(offsetof(Utrd, command_desc_base_addr) == 16, "UTRD UCD base DW4-5");
  check(offsetof(Utrd, prd_table_length) == 28, "UTRD PRDT DW7");
  check(sizeof(CommandUpiu) == 32, "command UPIU 32 bytes");

  CommandUpiu upiu{};
  buildReadCapacity10(upiu, 0, 3);
  check(upiu.cdb[0] == 0x25, "READ CAPACITY(10) opcode");
  check(upiu.task_tag == 3, "task tag");
  check(upiu.transaction_type == 0x00, "COMMAND UPIU");

  buildTestUnitReady(upiu, 1, 4);
  check(upiu.cdb[0] == 0x00, "TEST UNIT READY opcode");
  check(upiu.cdb[1] == (1u << 5), "LUN in CDB byte 1");

  // --- Command path against the fake controller ----------------------------
  FakeRegs fake;
  Hci hci(fake);
  check(!hci.status().ready, "not ready before enable");
  check(hci.enable(), "enable controller");
  check(hci.status().ready, "HCS ready after enable");
  check(!hci.status().host_error, "no host error");
  check(hci.disable(), "disable controller");
  check(!hci.status().ready, "not ready after disable");
  check(hci.enable(), "re-enable controller");

  uint32_t result = 0xFFFFFFFF;
  check(hci.uicCommand(kUicDmeGet, 0x00000001, 0, 0, &result), "UIC DME_GET completes");
  check(result == kUicResultSuccess, "UIC result success");
  check((fake.read32(kRegInterruptStatus) & kIsUicCommandCompl) == 0, "UIC interrupt cleared");
  check(hci.linkStartup(), "DME link startup");

  hci.setTransferList(0x0000000880000000ULL);
  check(fake.read32(kRegUtpTransferReqListBaseL) == 0x80000000u, "transfer list base low");
  check(fake.read32(kRegUtpTransferReqListBaseH) == 0x00000008u, "transfer list base high");

  check(hci.ringDoorbell(1u << 2), "ring doorbell");
  check(fake.rung == 1, "doorbell written once");
  check(hci.doorbellCleared(1u << 2), "doorbell cleared by controller");

  // A wedged controller must fail, not spin forever.
  FakeRegs stuck;
  stuck.stuck = true;
  Hci broken(stuck);
  check(!broken.enable(), "enable times out on a stuck controller");

  if (failures) {
    std::printf("%d failures\n", failures);
    return 1;
  }
  std::printf("SM8450 UFS: %u GCC branches, UFSHCI command path checks passed\n", kBranchCount);
  return 0;
}
