// SPDX-License-Identifier: BSD-2-Clause-Patent
// Cupid bring-up UFS console log. Every tick the kernel message buffer's tail is
// copied into the UFS log window opened by NativeUfs: block 0 is a plain-text
// header, blocks 1..32 are the last 128 KiB of console text in order (oldest first),
// so a boot that dies right after UFS starts still leaves its text on the medium.
// Reads back with: dd over the window + `strings`.
#include <IOKit/IOLib.h>
#include <kern/thread.h>
#include <string.h>

struct cupid_msgbuf {
    int magic;
    int size;
    int bufx;
    int bufr;
    char *bufc;
};
extern struct cupid_msgbuf *msgbufp;

extern int native_ufs_log_open(uint64_t *, uint64_t *, const char **) __attribute__((weak_import));
extern int native_ufs_log_write(uint64_t, uint32_t, const void *) __attribute__((weak_import));
extern int native_ufs_log_sync(void) __attribute__((weak_import));

#define CUPID_LOG_TEXT_BLOCKS 32
#define CUPID_LOG_BLOCK 4096
#define CUPID_LOG_TEXT_BYTES (CUPID_LOG_TEXT_BLOCKS * CUPID_LOG_BLOCK)
#define MSG_MAGIC_VALUE 0x063061

volatile unsigned int boot_log_state, boot_log_rounds, boot_log_errors;
const char *volatile boot_log_open_refusal = "log not opened yet";

static uint64_t log_region_blocks;
static IOLock *log_lock;
static uint8_t log_header[CUPID_LOG_BLOCK];
static uint8_t log_text[CUPID_LOG_TEXT_BYTES];
static int log_last_bufx = -1, log_last_written = -1;

// Copy the message buffer's tail into log_text, oldest byte first, so the newest
// line ends up at the end of the region and `strings | tail` shows it.
static unsigned log_snapshot(struct cupid_msgbuf *m)
{
    const unsigned size = (unsigned) m->size;
    int bufx = m->bufx;
    int bufr = m->bufr;
    if (size == 0 || size > 4u * 1024 * 1024) return 0;
    if (bufx < 0 || bufx > (int) size) bufx = 0;
    if (bufr < 0 || bufr > (int) size) bufr = 0;
    unsigned used = bufx >= bufr ? (unsigned) (bufx - bufr) : size - (unsigned) bufr + (unsigned) bufx;
    if (used > size) used = size;
    if (used > CUPID_LOG_TEXT_BYTES) used = CUPID_LOG_TEXT_BYTES;
    memset(log_text, 0, sizeof log_text);
    uint8_t *out = log_text + (CUPID_LOG_TEXT_BYTES - used);
    unsigned start = (used == 0) ? (unsigned) bufx : (unsigned) (bufx - (int) used);
    if (start > (unsigned) bufx && used) start = 0;   // wrapped past the end
    unsigned first = size - start;
    if (first > used) first = used;
    if (first) memcpy(out, m->bufc + start, first);
    if (used > first) memcpy(out + first, m->bufc, used - first);
    return used;
}

void boot_log_tick(unsigned now)
{
    (void) now;
    if (boot_log_state == 3) return;

    if (boot_log_state == 0) {
        if (!&native_ufs_log_open || !&native_ufs_log_write || !&native_ufs_log_sync) {
            boot_log_open_refusal = "ufs log not linked";
            boot_log_state = 3;
            return;
        }
        uint64_t first = 0, blocks = 0;
        const char *why = "";
        if (native_ufs_log_open(&first, &blocks, &why) != 1 ||
            blocks < (uint64_t) (CUPID_LOG_TEXT_BLOCKS + 1)) {
            boot_log_open_refusal = why && why[0] ? why : "log open refused";
            boot_log_state = 3;
            return;
        }
        log_region_blocks = blocks;
        if (!log_lock) log_lock = IOLockAlloc();
        boot_log_state = 2;
    }
    if (boot_log_state != 2) return;

    struct cupid_msgbuf *m = msgbufp;
    if (!m || m->magic != MSG_MAGIC_VALUE || !m->bufc || m->size <= 0) {
        boot_log_open_refusal = "msgbufp missing or bad magic (no console buffer)";
        boot_log_errors++;
        return;
    }

    if (log_lock) IOLockLock(log_lock);
    const unsigned used = log_snapshot(m);
    const int bufx = m->bufx;
    if (used && bufx == log_last_bufx) {
        if (log_lock) IOLockUnlock(log_lock);
        return;                                  // nothing new since the last flush
    }
    memset(log_header, 0, sizeof log_header);
    snprintf((char *) log_header, CUPID_LOG_BLOCK,
             "CUPIDKLOG1 rounds=%u errors=%u bytes=%u bufx=%d size=%d\n"
             "text blocks 1..%u of the UFS log window, oldest first\n",
             boot_log_rounds, boot_log_errors, used, bufx, m->size, CUPID_LOG_TEXT_BLOCKS);
    int ok = native_ufs_log_write(0, 1, log_header) &&
             native_ufs_log_write(1, CUPID_LOG_TEXT_BLOCKS, log_text);
    if (ok) native_ufs_log_sync();
    if (ok) {
        boot_log_rounds = boot_log_rounds + 1;
        log_last_bufx = bufx;
        log_last_written = (int) used;
    } else {
        boot_log_open_refusal = "UFS log write failed";
        boot_log_errors = boot_log_errors + 1;
    }
    if (log_lock) IOLockUnlock(log_lock);
}
// rebuild 2026-11-25: retrigger after the runner capacity failure
// rebuild 2: the hosted macOS runner was out of capacity again
