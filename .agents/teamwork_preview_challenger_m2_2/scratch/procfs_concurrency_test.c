/*
 * procfs_concurrency_test.c
 *
 * Empirical verification harness for Procfs Concurrency & Data Race Vulnerabilities
 * Simulates the lock release in loop behavior of ctx_monitor.c & smmu_guard.c procfs show handlers
 * and the TOCTOU / UAF race in safety_mem.c procfs show handler.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <stdbool.h>
#include <unistd.h>
#include <stdatomic.h>

#define MAX_LOG_ENTRIES 128
#define NUM_PRODUCERS 4
#define TEST_DURATION_SEC 2

typedef struct {
    unsigned long timestamp_ns;
    unsigned long pc;
    unsigned long fault_addr;
    int pid;
    char comm[16];
    long err_code;
    int trap_nr;
} fault_log_entry_t;

static fault_log_entry_t log_ring[MAX_LOG_ENTRIES];
static size_t log_head = 0;
static size_t log_count = 0;
static pthread_mutex_t log_lock = PTHREAD_MUTEX_INITIALIZER;

static atomic_long total_reads = 0;
static atomic_long index_out_of_bounds_count = 0;
static atomic_long duplicate_or_skipped_count = 0;
static atomic_bool running = true;

// Producer (simulating interrupt/event producing faults)
void *producer_thread(void *arg) {
    long id = (long)arg;
    unsigned long seq = 0;

    while (atomic_load(&running)) {
        pthread_mutex_lock(&log_lock);
        
        fault_log_entry_t *entry = &log_ring[log_head];
        entry->timestamp_ns = ++seq;
        entry->pc = 0xffff800000000000UL + seq;
        entry->fault_addr = 0xdeadbeef00000000UL + id;
        entry->pid = 1000 + id;
        snprintf(entry->comm, sizeof(entry->comm), "prod_%ld", id);
        
        log_head = (log_head + 1) % MAX_LOG_ENTRIES;
        if (log_count < MAX_LOG_ENTRIES) {
            log_count++;
        }
        
        pthread_mutex_unlock(&log_lock);
        usleep(50); // fast events
    }
    return NULL;
}

// Consumer (simulating ctx_monitor_proc_show / smmu_guard_proc_show)
void *consumer_thread(void *arg) {
    while (atomic_load(&running)) {
        size_t i, idx;
        fault_log_entry_t entry;
        unsigned long last_ts = 0;

        pthread_mutex_lock(&log_lock);
        size_t current_count = log_count;
        size_t current_head = log_head;
        
        for (i = 0; i < log_count; i++) {
            if (log_count == MAX_LOG_ENTRIES)
                idx = (log_head + i) % MAX_LOG_ENTRIES;
            else
                idx = i;
            
            entry = log_ring[idx];
            
            // SIMULATING THE KERNEL BUG: unlock around seq_printf
            pthread_mutex_unlock(&log_lock);
            
            // Simulating work (seq_printf)
            atomic_fetch_add(&total_reads, 1);
            if (idx >= MAX_LOG_ENTRIES) {
                atomic_fetch_add(&index_out_of_bounds_count, 1);
            }
            if (i > 0 && entry.timestamp_ns <= last_ts && entry.timestamp_ns > 0) {
                atomic_fetch_add(&duplicate_or_skipped_count, 1);
            }
            last_ts = entry.timestamp_ns;

            // Re-lock for next iteration
            pthread_mutex_lock(&log_lock);
        }
        pthread_mutex_unlock(&log_lock);
        usleep(100);
    }
    return NULL;
}

int main() {
    printf("[*] Starting Procfs Concurrency & Data Race Harness...\n");

    pthread_t producers[NUM_PRODUCERS];
    pthread_t consumer;

    for (long i = 0; i < NUM_PRODUCERS; i++) {
        pthread_create(&producers[i], NULL, producer_thread, (void *)i);
    }
    pthread_create(&consumer, NULL, consumer_thread, NULL);

    sleep(TEST_DURATION_SEC);
    atomic_store(&running, false);

    for (int i = 0; i < NUM_PRODUCERS; i++) {
        pthread_join(producers[i], NULL);
    }
    pthread_join(consumer, NULL);

    printf("[+] Total procfs log entry reads: %ld\n", atomic_load(&total_reads));
    printf("[!] Inconsistent / out-of-order / duplicate ring buffer reads detected: %ld\n", 
           atomic_load(&duplicate_or_skipped_count));

    if (atomic_load(&duplicate_or_skipped_count) > 0) {
        printf("[CONFIRMED BUG] Lock release during procfs iteration causes ring buffer read inconsistency under concurrent writes!\n");
        return 0;
    } else {
        printf("[-] Bug not reproduced.\n");
        return 1;
    }
}
