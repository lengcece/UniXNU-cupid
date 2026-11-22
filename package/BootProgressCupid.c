// SPDX-License-Identifier: MIT
// Cupid bring-up page: stage history, console, process count, free pages and
// UFS counters on the firmware's scanout buffer. Uses the sheng core/display
// headers but only the stable kernel/board symbols this port defines.

#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <pexpert/pexpert.h>
#include <kern/thread.h>
#include <kern/clock.h>
#include <kern/sched_prim.h>
#include <kern/processor.h>
#include <kern/misc_protos.h>
#include <kern/boot_progress.h>
#include <IOKit/IOLib.h>

#include "BootProgressCore.h"
#include "BootProgressDisplay.h"
#include "BootProgressPulse.h"

extern int nprocs;
extern unsigned int vm_page_free_count;
extern volatile uint64_t native_ufs_block_reads;
extern volatile uint64_t native_ufs_verified_blocks;
extern volatile uint64_t native_ufs_block_failures;

struct bp_panic_info boot_progress_panic_vinfo;
char boot_progress_panic_text[2048];

static struct bp_core state;
static struct bp_display display;
static unsigned enabled, started;
static unsigned bind_count, bind_result;
static uintptr_t bind_base;
static unsigned bind_depth;
static uint64_t frequency, epoch;
static volatile unsigned render_busy;
static volatile unsigned irq_count;

static uint64_t
ticks(void)
{
	uint64_t value;
	__asm__ volatile ("mrs %0, cntpct_el0" : "=r" (value));
	return value;
}

static unsigned
seconds(void)
{
	return (unsigned)((ticks() - epoch) / frequency);
}

static int
take(void)
{
	return __atomic_exchange_n(&render_busy, 1, __ATOMIC_ACQUIRE) == 0;
}

static void
release(void)
{
	__atomic_store_n(&render_busy, 0, __ATOMIC_RELEASE);
}

static void
draw(unsigned now)
{
	char row[BP_COLS];

	if (!display.bound) {
		return;
	}
	snprintf(row, sizeof(row), "cupid bring-up t=%us idle=%us IRQ=%u procs=%d free=%u",
	    now, now - state.activity_at, irq_count, nprocs, vm_page_free_count);
	bp_line(&display, 0, row, 0xffffffff);
	snprintf(row, sizeof(row), "UFS reads=%llu verified=%llu fail=%llu",
	    (unsigned long long)native_ufs_block_reads,
	    (unsigned long long)native_ufs_verified_blocks,
	    (unsigned long long)native_ufs_block_failures);
	bp_line(&display, 1, row, 0xff00ff00);
	for (unsigned i = 0; i < BP_HISTORY; i++) {
		unsigned index = (state.history_count + BP_HISTORY - 1 - i) % BP_HISTORY;
		if (!state.history[index].text[0]) {
			continue;
		}
		snprintf(row, sizeof(row), "%3us %s", state.history[index].seconds, state.history[index].text);
		bp_line(&display, 3 + (BP_HISTORY - 1 - i), row, 0xffffff00);
	}
	for (unsigned i = 0; i < BP_LOGS; i++) {
		unsigned index = (state.log_row + 1 + i) % BP_LOGS;
		if (!state.logs[index][0]) {
			continue;
		}
		bp_line(&display, 10 + i, state.logs[index], 0xffc0c0c0);
	}
}

static void
monitor(void *arg, wait_result_t result)
{
	(void)arg;
	(void)result;
	for (;;) {
		IOSleep(1000);
		if (!enabled) {
			continue;
		}
		boot_progress_poll();
		if (!take()) {
			continue;
		}
		draw(seconds());
		release();
	}
}

void
boot_progress_init(void)
{
	unsigned option = 0;

	if (!PE_parse_boot_argn("boot-progress", &option, sizeof(option)) ||
	    (option != 1 && option != 2)) {
		return;
	}
	__asm__ volatile ("mrs %0, cntfrq_el0" : "=r" (frequency));
	if (!frequency) {
		return;
	}
	epoch = ticks();
	state.limit = 120;
	state.stall = 30;
	state.continuous = option == 2;
	enabled = option;
	boot_progress_stage("PE initialized; early native mappings");
}

void
boot_progress_bind(uintptr_t base, unsigned width, unsigned height, unsigned rowbytes, unsigned depth)
{
	if (!enabled || !take()) {
		return;
	}
	bind_count++;
	bind_base = base;
	bind_depth = depth;
	bind_result = 1;
	if (base >= 0xffff000000000000ULL && depth == 32 &&
	    width >= 720 && width <= 4096 && height >= 560 && height <= 4096 &&
	    rowbytes >= width * 4 && rowbytes <= 65536 &&
	    base <= UINTPTR_MAX - (uint64_t)height * rowbytes) {
		display.frame = base;
		display.width = width;
		display.height = height;
		display.stride = rowbytes;
		display.scale = width >= 2160 && height >= 1400 ? 3 : 1;
		memset(display.previous, 0xff, sizeof(display.previous));
		for (unsigned i = 0; i < 48; i++) {
			display.previous[i][BP_COLS - 1] = 0;
		}
		display.bound = 1;
		bind_result = 0;
		bp_panic_bind(&boot_progress_panic_vinfo, base, width, height, rowbytes);
		bp_clear(&display);
		draw(seconds());
	}
	release();
}

int
boot_progress_active(void)
{
	return enabled != 0;
}

void
boot_progress_putc(char c)
{
	unsigned now;

	if (!enabled || !take()) {
		return;
	}
	bp_char(&state, c);
	now = seconds();
	if (!started && c == '\n') {
		draw(now);
	}
	release();
}

void
boot_progress_stage(const char *text)
{
	unsigned now;

	if (!enabled || !take()) {
		return;
	}
	now = seconds();
	bp_stage(&state, text, now);
	if (!started) {
		draw(now);
	}
	release();
}

void
boot_progress_irq(uint64_t pc, uint64_t psr, uint64_t sp)
{
	(void)pc;
	(void)psr;
	(void)sp;
	__atomic_add_fetch(&irq_count, 1, __ATOMIC_RELAXED);
}

void
boot_progress_poll(void)
{
	unsigned now, reason;

	if (!enabled || !take()) {
		return;
	}
	now = seconds();
	reason = bp_poll(&state, now, native_ufs_block_reads, native_ufs_verified_blocks,
	    native_ufs_block_failures);
	if (reason && !state.continuous) {
		draw(now);
	}
	release();
}

void
boot_progress_start(void)
{
	thread_t thread;

	if (!enabled || started) {
		return;
	}
	started = 1;
	if (kernel_thread_start_priority(monitor, NULL, BASEPRI_KERNEL, &thread) != KERN_SUCCESS) {
		started = 0;
		boot_progress_stage("monitor thread creation FAILED");
		return;
	}
	thread_deallocate(thread);
	boot_progress_stage(state.continuous ?
	    "monitor continuous: deadlines warn only" :
	    "monitor armed: idle 30s / total 120s");
}

void
boot_progress_user_breakpoint(void *statep, unsigned esr)
{
	(void)statep;
	(void)esr;
}

void
boot_progress_user_abort(void *statep, unsigned esr, uint64_t fault)
{
	(void)statep;
	(void)esr;
	(void)fault;
}

void
boot_progress_critical_exit(int pid, char *name, unsigned long code)
{
	char row[BP_COLS];

	if (!enabled) {
		return;
	}
	snprintf(row, sizeof(row), "critical exit pid=%d %.40s code=%lu", pid, name ? name : "", code);
	boot_progress_stage(row);
}

void
boot_progress_process_stage(int pid, const char *name, const char *text)
{
	char row[BP_COLS];

	if (!enabled) {
		return;
	}
	snprintf(row, sizeof(row), "pid=%d %.24s %.60s", pid, name ? name : "", text ? text : "");
	boot_progress_stage(row);
}

void
boot_progress_exit(int pid, const char *name, int status, unsigned ns, uint64_t code)
{
	char row[BP_COLS];

	if (!enabled) {
		return;
	}
	snprintf(row, sizeof(row), "exit pid=%d %.24s status=%d %uns code=%llu",
	    pid, name ? name : "", status, ns, (unsigned long long)code);
	boot_progress_stage(row);
}
