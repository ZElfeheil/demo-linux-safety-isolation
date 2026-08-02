/*
 * test_far_el1_filtering.c - Empirical test harness for FAR_EL1 address extraction & filtering
 * Tests 64-bit kernel virtual address filtering against protected ranges.
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <assert.h>

#define NOTIFY_OK 0x0001
#define DIE_PAGE_FAULT 0x000E

// Mock kernel structures matching ARM64 Linux kernel definitions
struct pt_regs {
	unsigned long regs[31];
	unsigned long sp;
	unsigned long pc;
	unsigned long pstate;
	unsigned long orig_x0;
	unsigned long syscallno;
	unsigned long orig_addr_limit;
	unsigned long pmr;
	unsigned long stackframe[2];
	unsigned long lockdep_depth;
	unsigned long zero_padding;
	unsigned long far;       // ARM64 Fault Address Register (FAR_EL1)
	unsigned long esr;       // ARM64 Exception Syndrome Register (ESR_EL1)
};

struct die_args {
	struct pt_regs *regs;
	const char *str;
	long err;
	int trapnr;
	int signr;
};

// Module state variables
static unsigned long protected_range_start = 0;
static unsigned long protected_range_end = 0;
static bool g_fault_logged = false;
static unsigned long g_logged_fault_addr = 0;
static unsigned long g_logged_pc = 0;

static void ctx_monitor_set_protected_range(unsigned long start, unsigned long end)
{
	protected_range_start = start;
	protected_range_end = end;
}

static int ctx_die_notifier_cb(unsigned long val, void *data)
{
	struct die_args *args = (struct die_args *)data;
	struct pt_regs *regs;
	unsigned long fault_addr;

	if (val != DIE_PAGE_FAULT)
		return NOTIFY_OK;

	if (!args || !args->regs)
		return NOTIFY_OK;

	regs = args->regs;
	fault_addr = (unsigned long)regs->far;

	if (protected_range_start != 0 && protected_range_end != 0) {
		if (fault_addr < protected_range_start || fault_addr >= protected_range_end)
			return NOTIFY_OK;
	}

	// Simulated logging
	g_fault_logged = true;
	g_logged_fault_addr = fault_addr;
	g_logged_pc = regs->pc;

	return NOTIFY_OK;
}

// Old buggy callback implementation for comparison
static int ctx_die_notifier_cb_BUGGY(unsigned long val, void *data)
{
	struct die_args *args = (struct die_args *)data;
	struct pt_regs *regs;
	unsigned long fault_addr;

	if (val != DIE_PAGE_FAULT)
		return NOTIFY_OK;

	if (!args || !args->regs)
		return NOTIFY_OK;

	regs = args->regs;
	// BUGGY: line 105 extracted args->trapnr instead of regs->far!
	fault_addr = (unsigned long)args->trapnr;

	if (protected_range_start != 0 && protected_range_end != 0) {
		if (fault_addr < protected_range_start || fault_addr >= protected_range_end)
			return NOTIFY_OK;
	}

	g_fault_logged = true;
	g_logged_fault_addr = fault_addr;
	g_logged_pc = regs->pc;

	return NOTIFY_OK;
}

int main(void)
{
	printf("=== Running FAR_EL1 Address Extraction & Filtering Test Harness ===\n\n");

	// Define typical 64-bit kernel virtual address protected range
	// On ARM64 Linux, kernel space addresses use TTBR1_EL1 and begin with 0xffff...
	unsigned long page_start = 0xffff800012345000UL;
	unsigned long page_end   = 0xffff800012346000UL; // 4KB page

	ctx_monitor_set_protected_range(page_start, page_end);

	struct pt_regs regs;
	struct die_args args;

	regs.pc = 0xffff800010008888UL;
	args.regs = &regs;
	args.err = 0x9600004f;
	args.trapnr = 14; // DIE_PAGE_FAULT trap vector number (32-bit int)

	int failed = 0;

	// Test 1: Fault inside protected range (0xffff800012345100)
	regs.far = 0xffff800012345100UL;
	g_fault_logged = false;
	ctx_die_notifier_cb(DIE_PAGE_FAULT, &args);

	if (!g_fault_logged || g_logged_fault_addr != 0xffff800012345100UL) {
		printf("[FAIL] Test 1: Expected fault_addr 0x%lx logged, got logged=%d, addr=0x%lx\n",
		       regs.far, g_fault_logged, g_logged_fault_addr);
		failed++;
	} else {
		printf("[PASS] Test 1: 64-bit kernel VA 0x%lx in protected range correctly TRAPPED & LOGGED\n",
		       regs.far);
	}

	// Test 2: Verify buggy implementation failed on Test 1
	g_fault_logged = false;
	ctx_die_notifier_cb_BUGGY(DIE_PAGE_FAULT, &args);
	if (!g_fault_logged) {
		printf("[CONFIRMED] Buggy code failed on Test 1: trapnr (%d) fell outside kernel VA range (0x%lx - 0x%lx)\n",
		       args.trapnr, page_start, page_end);
	} else {
		printf("[FAIL] Buggy code unexpectedly logged fault\n");
		failed++;
	}

	// Test 3: Fault at start bound of protected range (0xffff800012345000)
	regs.far = 0xffff800012345000UL;
	g_fault_logged = false;
	ctx_die_notifier_cb(DIE_PAGE_FAULT, &args);

	if (!g_fault_logged || g_logged_fault_addr != 0xffff800012345000UL) {
		printf("[FAIL] Test 3: Start boundary fault failed. logged=%d, addr=0x%lx\n",
		       g_fault_logged, g_logged_fault_addr);
		failed++;
	} else {
		printf("[PASS] Test 3: Start boundary 64-bit VA 0x%lx correctly TRAPPED & LOGGED\n", regs.far);
	}

	// Test 4: Fault at end bound (outside: 0xffff800012346000)
	regs.far = 0xffff800012346000UL;
	g_fault_logged = false;
	ctx_die_notifier_cb(DIE_PAGE_FAULT, &args);

	if (g_fault_logged) {
		printf("[FAIL] Test 4: End boundary 0x%lx SHOULD BE IGNORED but was logged!\n", regs.far);
		failed++;
	} else {
		printf("[PASS] Test 4: Out-of-range end 64-bit VA 0x%lx correctly FILTERED OUT\n", regs.far);
	}

	// Test 5: Fault far outside kernel VA range (e.g. 0xffff800099999000)
	regs.far = 0xffff800099999000UL;
	g_fault_logged = false;
	ctx_die_notifier_cb(DIE_PAGE_FAULT, &args);

	if (g_fault_logged) {
		printf("[FAIL] Test 5: External kernel VA 0x%lx SHOULD BE IGNORED but was logged!\n", regs.far);
		failed++;
	} else {
		printf("[PASS] Test 5: External kernel VA 0x%lx correctly FILTERED OUT\n", regs.far);
	}

	// Test 6: Protection disabled (range 0 to 0)
	ctx_monitor_set_protected_range(0, 0);
	regs.far = 0xffff800012345100UL;
	g_fault_logged = false;
	ctx_die_notifier_cb(DIE_PAGE_FAULT, &args);

	if (!g_fault_logged) {
		printf("[FAIL] Test 6: When range is (0,0), all faults should be logged, but was ignored\n");
		failed++;
	} else {
		printf("[PASS] Test 6: Range (0,0) global mode correctly logged fault 0x%lx\n", regs.far);
	}

	if (failed == 0) {
		printf("\n=== VERDICT: ALL FAR_EL1 EXTRACTION & FILTERING TESTS PASSED ===\n");
		return 0;
	} else {
		printf("\n=== VERDICT: %d FAR_EL1 TESTS FAILED ===\n", failed);
		return 1;
	}
}
