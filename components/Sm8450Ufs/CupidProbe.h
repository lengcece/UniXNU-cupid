// SPDX-License-Identifier: BSD-2-Clause-Patent
#ifndef CUPID_PROBE_H
#define CUPID_PROBE_H

// Optional probes report a result and return. A FAIL must never publish a
// successful hardware resource or touch later MMIO stages that need it.
enum CupidProbeState : unsigned {
  CupidProbePass = 1, CupidProbeFail = 2, CupidProbeSkip = 3, CupidProbeRunning = 4
};

static inline void CupidProbeRecord(unsigned slot, const char *name,
                                    CupidProbeState state, const char *reason,
                                    uint64_t detail = 0)
{
  (void)slot;
  (void)name;
  (void)state;
  (void)reason;
  (void)detail;
}
#endif
