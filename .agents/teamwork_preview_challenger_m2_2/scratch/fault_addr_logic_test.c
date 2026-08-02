/*
 * fault_addr_logic_test.c
 *
 * Empirical verification harness for ctx_monitor.c line 105 bug:
 * fault_addr = (unsigned long)args->trapnr;
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <stdint.h>

struct pt_regs {
    uint64_t pc;
    uint64_t pstate;
};

struct die_args {
    struct pt_regs *regs;
    const char *str;
    long err;
    int trapnr;
    int signr;
};

// Simulated ctx_monitor state
static unsigned long protected_range_start = 0xffff800000100000UL;
static unsigned long protected_range_end   = 0xffff800000200000UL;

static bool logged_event = false;
static unsigned long logged_fault_addr = 0;

static void log_fault_event(struct pt_regs *regs, unsigned long fault_addr, long err, int trapnr) {
    logged_event = true;
    logged_fault_addr = fault_addr;
}

// Reproduction of ctx_die_notifier_cb from kernel/ctx_monitor/ctx_monitor.c
static int ctx_die_notifier_cb_kernel_version(struct die_args *args) {
    struct pt_regs *regs;
    unsigned long fault_addr;

    logged_event = false;
    regs = args->regs;

    // BUG IN KERNEL CODE LINE 105:
    fault_addr = (unsigned long)args->trapnr;

    if (protected_range_start != 0 && protected_range_end != 0) {
        if (fault_addr < protected_range_start || fault_addr >= protected_range_end)
            return 0; // NOTIFY_OK without logging
    }

    log_fault_event(regs, fault_addr, args->err, args->trapnr);
    return 1;
}

// Fixed version using actual faulting address (e.g. from FAR_EL1 / err)
static int ctx_die_notifier_cb_fixed_version(struct die_args *args, unsigned long actual_far) {
    struct pt_regs *regs;
    unsigned long fault_addr;

    logged_event = false;
    regs = args->regs;

    // FIX: use actual fault address FAR_EL1
    fault_addr = actual_far;

    if (protected_range_start != 0 && protected_range_end != 0) {
        if (fault_addr < protected_range_start || fault_addr >= protected_range_end)
            return 0;
    }

    log_fault_event(regs, fault_addr, args->err, args->trapnr);
    return 1;
}

int main() {
    printf("[*] Starting Exception Vector vs. Fault Address Harness...\n");

    struct pt_regs regs = { .pc = 0xffff800000100040UL };
    struct die_args args = {
        .regs = &regs,
        .str = "page fault",
        .err = 0x96000047, // ESR_EL1 value for permission fault
        .trapnr = 14,      // DIE_PAGE_FAULT trap vector number
        .signr = 11
    };
    unsigned long actual_fault_virtual_addr = 0xffff800000100080UL; // Inside protected range!

    // 1. Run Kernel Version
    ctx_die_notifier_cb_kernel_version(&args);
    bool kernel_logged = logged_event;

    printf("Kernel Code Result:\n");
    printf("  - Protected Range: [0x%lx - 0x%lx]\n", protected_range_start, protected_range_end);
    printf("  - Target Fault Virtual Address: 0x%lx (INSIDE protected range)\n", actual_fault_virtual_addr);
    printf("  - Assigned fault_addr in code: 0x%lx (args->trapnr = 14)\n", (unsigned long)args.trapnr);
    printf("  - Event Logged by Kernel Code? %s\n", kernel_logged ? "YES" : "NO (DROPPED!)");

    // 2. Run Fixed Version
    ctx_die_notifier_cb_fixed_version(&args, actual_fault_virtual_addr);
    bool fixed_logged = logged_event;

    printf("\nFixed Code Result:\n");
    printf("  - Event Logged by Fixed Code? %s\n", fixed_logged ? "YES" : "NO");
    printf("  - Logged Fault Address: 0x%lx\n", logged_fault_addr);

    if (!kernel_logged && fixed_logged) {
        printf("\n[CONFIRMED BUG] ctx_monitor.c line 105 assigns trapnr (14) to fault_addr, breaking range checking and silently dropping page fault logs when protected range is configured!\n");
        return 0;
    } else {
        printf("\n[-] Bug not reproduced.\n");
        return 1;
    }
}
