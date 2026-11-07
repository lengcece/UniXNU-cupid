// SPDX-License-Identifier: BSD-2-Clause-Patent
// SM8450 UFS host-controller (UFSHCI) command path — host-testable core.
//
// Register offsets and status bits are transcribed from the public Linux UFSHCI
// definitions:
//   include/ufs/ufshci.h (torvalds/linux)
// Note that current UFSHCI places UIC at 0x90 and HCE at 0x34; older 3.0
// summaries that use 0x50/0x1C are wrong for this controller.
//
// The DT compatible string is the one the vendor and mainline SM8450 trees use:
//   qcom,sm8450-ufshc
//
// No hardware is touched here. Hci drives a RegOps implementation; the kext
// supplies the real MMIO mapping later.
#ifndef SM8450_UFS_HCI_H
#define SM8450_UFS_HCI_H

#include <cstddef>
#include <cstdint>
#include <cstring>

#include "Sm8450UfsRegs.h"

namespace sm8450_ufs {

inline constexpr char kCompatible[] = "qcom,sm8450-ufshc";

inline bool compatibleMatches(const char* compatible) {
  return compatible != nullptr && std::strcmp(compatible, kCompatible) == 0;
}

// UFSHCI registers.
enum : uint32_t {
  kRegCapabilities = 0x00,
  kRegVersion = 0x08,
  kRegControllerPid = 0x10,
  kRegControllerMid = 0x14,
  kRegInterruptStatus = 0x20,
  kRegInterruptEnable = 0x24,
  kRegControllerStatus = 0x30,
  kRegControllerEnable = 0x34,
  kRegUicErrorPhy = 0x38,
  kRegUicErrorDl = 0x3C,
  kRegUicErrorNl = 0x40,
  kRegUicErrorTl = 0x44,
  kRegUicErrorDme = 0x48,
  kRegUtpTransferReqListBaseL = 0x50,
  kRegUtpTransferReqListBaseH = 0x54,
  kRegUtpTransferReqDoorBell = 0x58,
  kRegUtpTransferReqListClear = 0x5C,
  kRegUicCommand = 0x90,
  kRegUicCommandArg1 = 0x94,
  kRegUicCommandArg2 = 0x98,
  kRegUicCommandArg3 = 0x9C,
  kUfshciRegisterSpace = 0xA0,
};

// HCS - Host Controller Status.
enum : uint32_t {
  kDevicePresent = 0x1,
  kUtpTransferReqListReady = 0x2,
  kUtpTaskReqListReady = 0x4,
  kUicCommandReady = 0x8,
  kHostErrorIndicator = 0x10,
  kDeviceErrorIndicator = 0x20,
  kStatusReady = kUtpTransferReqListReady | kUtpTaskReqListReady | kUicCommandReady,
};

// HCE - Host Controller Enable.
enum : uint32_t {
  kControllerEnable = 0x1,
  kControllerDisable = 0x0,
};

// IS - Interrupt Status.
enum : uint32_t {
  kIsUtpTransferReqCompl = 0x1,
  kIsUicCommandCompl = 0x400,
  kIsUtpError = 0x1000,
  kIsControllerFatal = 0x10000,
};

// UIC command opcodes.
enum : uint8_t {
  kUicDmeGet = 0x01,
  kUicDmeSet = 0x02,
  kUicDmeLinkStartup = 0x16,
  kUicDmeEndPointReset = 0x15,
};

enum : uint32_t { kUicResultSuccess = 0x00 };

// UTP Transfer Request Descriptor (32 bytes).
// DW0-3 are kept as raw words: the header packs a bitfield layout that a plain
// byte-per-flag struct would pad to 40 bytes. The static asserts below bound it.
struct Utrd {
  uint32_t header[4];  // DW0-3: CCI/EHS, flags, DUNL, OCS/CDS/LDBC/DUNU
  uint64_t command_desc_base_addr;  // DW4-5
  uint16_t response_upiu_length;    // DW6
  uint16_t response_upiu_offset;
  uint16_t prd_table_length;  // DW7
  uint16_t prd_table_offset;
};
static_assert(sizeof(Utrd) == 32, "UTRD is 32 bytes");
static_assert(offsetof(Utrd, command_desc_base_addr) == 16, "UTRD DW4-5");
static_assert(offsetof(Utrd, response_upiu_length) == 24, "UTRD DW6");
static_assert(offsetof(Utrd, prd_table_length) == 28, "UTRD DW7");

enum : uint8_t {
  kOcsSuccess = 0x0,
  kMASK_OCS = 0x0F,
};

enum : uint8_t {
  kUtpScsiCommand = 0x00,
  kUtpCommandTypeUfsStorage = 0x1,
  kUtpDataToHost = 2,
  kUtpNoDataTransfer = 0,
};

// Command UPIU (32 bytes), UFS UPIU header DW0-DW2 + 16-byte CDB + 4 reserved.
struct CommandUpiu {
  uint8_t type_flags;   // b0: transaction type (0x00 = COMMAND) | flags
  uint8_t lun;          // b1
  uint8_t task_tag;     // b2
  uint8_t iid_cs;       // b3: IID (high nibble) | command set type (low nibble)
  uint8_t response;     // b4
  uint8_t status;       // b5
  uint8_t ehs_length;   // b6
  uint8_t device_info;  // b7
  uint32_t data_segment_length;  // b8-11, big-endian on the wire
  uint8_t cdb[16];      // b12-27
  uint32_t reserved;    // b28-31
};
static_assert(sizeof(CommandUpiu) == 32, "command UPIU is 32 bytes");
static_assert(offsetof(CommandUpiu, lun) == 1, "UPIU LUN at b1");
static_assert(offsetof(CommandUpiu, task_tag) == 2, "UPIU task tag at b2");
static_assert(offsetof(CommandUpiu, cdb) == 12, "UPIU CDB at b12");

// Response UPIU: 12-byte header + utp_cmd_rsp (40 bytes) = 52.
struct ResponseUpiu {
  uint8_t type_flags;   // b0
  uint8_t lun;          // b1
  uint8_t task_tag;     // b2
  uint8_t iid_cs;       // b3
  uint8_t response;     // b4
  uint8_t status;       // b5
  uint8_t ehs_length;   // b6
  uint8_t device_info;  // b7
  uint32_t data_segment_length;  // b8-11
  uint32_t residual_transfer_count;  // DW3
  uint32_t reserved[4];              // DW4-7
  uint16_t sense_data_len;           // DW8
  uint8_t sense_data[18];
};
static_assert(sizeof(ResponseUpiu) == 52, "response UPIU is 52 bytes");

// Physical Region Descriptor entry: 16 bytes; size is 4-byte granular.
struct PrdtEntry {
  uint64_t addr;
  uint32_t reserved;
  uint32_t size;
};
static_assert(sizeof(PrdtEntry) == 16, "PRDT entry is 16 bytes");

constexpr uint32_t kPrdtMaxBytes = 256 * 1024;
constexpr uint32_t kUcdCommandOffset = 0;
constexpr uint32_t kUcdResponseOffset = 512;
constexpr uint32_t kUcdPrdtOffset = 1024;

// The PRDT byte count must be padded to a 4-byte boundary.
constexpr uint32_t prdtSize(uint32_t bytes) { return (bytes + 3u) & ~3u; }

// Command completion: OCS lives in the UTRD, response/status in the UPIU.
inline uint8_t responseStatus(const ResponseUpiu& r) { return r.status; }
inline bool responseSuccess(const ResponseUpiu& r) { return r.response == 0 && r.status == 0; }

inline void initCommandUpiu(CommandUpiu& upiu, uint8_t lun, uint8_t task_tag, uint16_t data_bytes) {
  std::memset(&upiu, 0, sizeof(upiu));
  upiu.type_flags = 0x00;  // COMMAND UPIU, no flags
  upiu.lun = lun;
  upiu.task_tag = task_tag;
  upiu.iid_cs = 0;  // IID 0, SCSI command set
  upiu.data_segment_length = data_bytes;
}

inline void buildReadCapacity10(CommandUpiu& upiu, uint8_t lun, uint8_t task_tag) {
  initCommandUpiu(upiu, lun, task_tag, 0);
  upiu.cdb[0] = 0x25;  // READ CAPACITY(10)
  upiu.cdb[1] = static_cast<uint8_t>(lun << 5);
}

inline void buildTestUnitReady(CommandUpiu& upiu, uint8_t lun, uint8_t task_tag) {
  initCommandUpiu(upiu, lun, task_tag, 0);
  upiu.cdb[0] = 0x00;  // TEST UNIT READY
  upiu.cdb[1] = static_cast<uint8_t>(lun << 5);
}

// MMIO interface supplied by the caller (real mapping or a test fake).
struct RegOps {
  virtual ~RegOps() = default;
  virtual uint32_t read32(uint32_t offset) = 0;
  virtual void write32(uint32_t offset, uint32_t value) = 0;
  virtual void delayUs(uint32_t microseconds) = 0;
};

struct HciStatus {
  uint32_t hcs;
  bool ready;
  bool host_error;
  bool device_error;
};

// Read-only-in-tests command engine. Every wait polls a bounded number of
// times so a missing controller fails instead of hanging.
class Hci {
 public:
  static constexpr uint32_t kMaxPoll = 100000;

  explicit Hci(RegOps& regs) : regs_(regs) {}

  HciStatus status() {
    const uint32_t hcs = regs_.read32(kRegControllerStatus);
    return {hcs, (hcs & kStatusReady) == kStatusReady, (hcs & kHostErrorIndicator) != 0,
            (hcs & kDeviceErrorIndicator) != 0};
  }

  bool enable() {
    regs_.write32(kRegControllerEnable, kControllerEnable);
    return waitStatus(kStatusReady, true);
  }

  bool disable() {
    regs_.write32(kRegControllerEnable, kControllerDisable);
    return waitStatus(kControllerEnable, false);
  }

  uint32_t interruptStatus() { return regs_.read32(kRegInterruptStatus); }

  void clearInterrupts(uint32_t bits) { regs_.write32(kRegInterruptStatus, bits); }

  // UIC command; result2 carries the DME attribute value or result code.
  bool uicCommand(uint8_t opcode, uint32_t arg1, uint32_t arg2, uint32_t arg3,
                  uint32_t* result2 = nullptr) {
    if (!waitStatus(kUicCommandReady, true)) {
      return false;
    }
    clearInterrupts(kIsUicCommandCompl);
    regs_.write32(kRegUicCommandArg1, arg1);
    regs_.write32(kRegUicCommandArg2, arg2);
    regs_.write32(kRegUicCommandArg3, arg3);
    regs_.write32(kRegUicCommand, opcode);
    if (!waitInterrupt(kIsUicCommandCompl)) {
      return false;
    }
    if (result2 != nullptr) {
      *result2 = regs_.read32(kRegUicCommandArg2);
    }
    const bool ok = (regs_.read32(kRegInterruptStatus) & (kIsUicCommandCompl | kIsUtpError)) ==
                    kIsUicCommandCompl;
    clearInterrupts(kIsUicCommandCompl | kIsUtpError);
    return ok;
  }

  bool linkStartup() { return uicCommand(kUicDmeLinkStartup, 0, 0, 0); }

  void setTransferList(uint64_t base) {
    regs_.write32(kRegUtpTransferReqListBaseL, static_cast<uint32_t>(base & 0xFFFFFFFFu));
    regs_.write32(kRegUtpTransferReqListBaseH, static_cast<uint32_t>(base >> 32));
  }

  bool ringDoorbell(uint32_t slot_mask) {
    regs_.write32(kRegUtpTransferReqDoorBell, slot_mask);
    return waitInterrupt(kIsUtpTransferReqCompl);
  }

  bool doorbellCleared(uint32_t slot_mask) {
    for (uint32_t i = 0; i < kMaxPoll; ++i) {
      if ((regs_.read32(kRegUtpTransferReqDoorBell) & slot_mask) == 0) {
        return true;
      }
      regs_.delayUs(1);
    }
    return false;
  }

 private:
  bool waitStatus(uint32_t mask, bool set) {
    for (uint32_t i = 0; i < kMaxPoll; ++i) {
      const uint32_t hcs = regs_.read32(kRegControllerStatus);
      if (((hcs & mask) != 0) == set) {
        return true;
      }
      regs_.delayUs(1);
    }
    return false;
  }

  bool waitInterrupt(uint32_t bit) {
    for (uint32_t i = 0; i < kMaxPoll; ++i) {
      if ((regs_.read32(kRegInterruptStatus) & bit) != 0) {
        return true;
      }
      regs_.delayUs(1);
    }
    return false;
  }

  RegOps& regs_;
};

}  // namespace sm8450_ufs

#endif
