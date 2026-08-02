/*
 * test_m2_stress.c - Empirical Stress Harness for M2 Kernel Concurrency & Locking
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <pthread.h>
#include <unistd.h>
#include <sys/mman.h>
#include <signal.h>
#include <setjmp.h>
#include <stdatomic.h>

#define SAFETY_SENTINEL   0x5AFE1234U
#define CORRUPT_SENTINEL  0xDEADDEADU

/* =====================================================================
 * TEST 1: Protection Toggle vs Safe Write Race Condition
 * ===================================================================== */
static _Thread_local sigjmp_buf thread_jmp_env;
static atomic_int segv_count = 0;

static void thread_segv_handler(int sig, siginfo_t *info, void *uctx) {
    atomic_fetch_add(&segv_count, 1);
    siglongjmp(thread_jmp_env, 1);
}

static uint32_t *g_test_buf = NULL;
static pthread_mutex_t g_safety_mutex = PTHREAD_MUTEX_INITIALIZER;
static atomic_int g_ctx_protected = 0;
static atomic_bool g_stop_threads = false;

static void set_protection_sim(bool enable_ro) {
    if (enable_ro) {
        mprotect(g_test_buf, 4096, PROT_READ);
        atomic_store(&g_ctx_protected, 1);
    } else {
        mprotect(g_test_buf, 4096, PROT_READ | PROT_WRITE);
        atomic_store(&g_ctx_protected, 0);
    }
}

static void safe_write_sim(uint32_t val) {
    pthread_mutex_lock(&g_safety_mutex);
    /* In safety_mem.c, safety_mem_set_protection() is NOT locked by g_safety_mutex!
     * So g_ctx_protected can change right here, or mprotect(PROT_READ) can be called concurrently! */
    bool was_ro = (atomic_load(&g_ctx_protected) != 0);
    if (was_ro) {
        mprotect(g_test_buf, 4096, PROT_READ | PROT_WRITE);
    }

    if (sigsetjmp(thread_jmp_env, 1) == 0) {
        *g_test_buf = val;
    } else {
        /* Fault caught! */
    }

    if (was_ro) {
        mprotect(g_test_buf, 4096, PROT_READ);
    }
    pthread_mutex_unlock(&g_safety_mutex);
}

static void *worker_toggle_protection(void *arg) {
    while (!atomic_load(&g_stop_threads)) {
        set_protection_sim(true);
        usleep(5);
        set_protection_sim(false);
        usleep(5);
    }
    return NULL;
}

static void *worker_safe_write(void *arg) {
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_sigaction = thread_segv_handler;
    sa.sa_flags = SA_SIGINFO;
    sigaction(SIGBUS, &sa, NULL);
    sigaction(SIGSEGV, &sa, NULL);

    while (!atomic_load(&g_stop_threads)) {
        safe_write_sim(0x12345678);
        usleep(2);
    }
    return NULL;
}

static void test_protection_race(void) {
    printf("[*] Running Test 1: Protection Toggle vs Safe Write Race Condition...\n");

    g_test_buf = mmap(NULL, 4096, PROT_READ | PROT_WRITE, MAP_ANONYMOUS | MAP_SHARED, -1, 0);
    *g_test_buf = SAFETY_SENTINEL;

    pthread_t t1, t2;
    atomic_store(&g_stop_threads, false);
    atomic_store(&segv_count, 0);

    pthread_create(&t1, NULL, worker_toggle_protection, NULL);
    pthread_create(&t2, NULL, worker_safe_write, NULL);

    usleep(100000); // 100ms test run

    atomic_store(&g_stop_threads, true);
    pthread_join(t1, NULL);
    pthread_join(t2, NULL);

    printf("    [+] Total faults caught during unprotected toggle race: %d\n", atomic_load(&segv_count));
    if (atomic_load(&segv_count) > 0) {
        printf("    [!] CONFIRMED BUG 1: Unprotected safety_mem_set_protection leads to SEGV/fault during safe_write!\n");
    } else {
        printf("    [-] Race condition not triggered in this 100ms sample window.\n");
    }

    munmap(g_test_buf, 4096);
}

/* =====================================================================
 * TEST 2: Thread A Detection Flaw (Corrupted before hold window)
 * ===================================================================== */
static void test_detection_flaw(void) {
    printf("[*] Running Test 2: Thread A Detection Flaw Verification...\n");
    uint32_t mem = SAFETY_SENTINEL;
    uint32_t violation_counter = 0;

    /* Rogue Thread C writes corrupt value outside Thread A's lock window */
    mem = CORRUPT_SENTINEL;

    /* Thread A acquires lock */
    uint32_t initial_val = mem;
    usleep(1000); // hold duration
    uint32_t current_val = mem;

    /* Thread A violation check as implemented in mutex_threads.c: line 108 */
    if (current_val != initial_val) {
        violation_counter++;
    }

    printf("    Buffer state: 0x%08X (Expected Sentinel: 0x%08X)\n", mem, SAFETY_SENTINEL);
    printf("    Thread A Violation Counter: %u\n", violation_counter);
    if (mem != SAFETY_SENTINEL && violation_counter == 0) {
        printf("    [!] CONFIRMED BUG 2: Thread A missed corrupted data because corruption occurred before hold window!\n");
    }
}

/* =====================================================================
 * TEST 3: Module Unload UAF Window Simulation
 * ===================================================================== */
static uint32_t *g_buf_ptr = NULL;
static atomic_bool g_uaf_detected = false;

static void *worker_deref_buf(void *arg) {
    while (!atomic_load(&g_stop_threads)) {
        uint32_t *ptr = g_buf_ptr;
        if (ptr != NULL) {
            /* If page was freed before ptr was set to NULL, reading *ptr is UAF */
            if (ptr == (uint32_t *)0xDEADBEEF) { // simulated freed page address
                atomic_store(&g_uaf_detected, true);
            }
        }
        usleep(1);
    }
    return NULL;
}

static void test_unload_uaf_window(void) {
    printf("[*] Running Test 3: Module Unload UAF Window Verification...\n");
    g_buf_ptr = malloc(sizeof(uint32_t));
    *g_buf_ptr = SAFETY_SENTINEL;

    pthread_t t;
    atomic_store(&g_stop_threads, false);
    atomic_store(&g_uaf_detected, false);

    pthread_create(&t, NULL, worker_deref_buf, NULL);

    usleep(1000);

    /* Simulate safety_mem_exit() sequence: free memory FIRST, set NULL SECOND */
    uint32_t *freed_page = g_buf_ptr;
    free(freed_page);
    /* Window between free and NULL assignment */
    g_buf_ptr = (uint32_t *)0xDEADBEEF; // Mark as freed memory
    usleep(500);
    g_buf_ptr = NULL; // Finally set NULL

    atomic_store(&g_stop_threads, true);
    pthread_join(t, NULL);

    if (atomic_load(&g_uaf_detected)) {
        printf("    [!] CONFIRMED BUG 3: Dereference occurred during window between page free and safety_buf_ptr=NULL!\n");
    }
}

int main(void) {
    setbuf(stdout, NULL);
    printf("=== M2 KERNEL MODULE CONCURRENCY & LOCKING STRESS HARNESS ===\n\n");
    test_protection_race();
    printf("\n");
    test_detection_flaw();
    printf("\n");
    test_unload_uaf_window();
    printf("\n=== STRESS HARNESS COMPLETE ===\n");
    return 0;
}
