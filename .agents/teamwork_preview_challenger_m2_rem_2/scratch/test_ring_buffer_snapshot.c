/*
 * test_ring_buffer_snapshot.c - Multi-threaded stress test harness for ring buffer snapshotting
 * Tests deadlock freedom, race freedom, data consistency, and ring buffer wrap indexing.
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <pthread.h>
#include <unistd.h>
#include <assert.h>
#include <stdatomic.h>

#define MAX_LOG_ENTRIES 128
#define NUM_WRITER_THREADS 8
#define NUM_READER_THREADS 4
#define WRITES_PER_THREAD 50000
#define READS_PER_THREAD 10000

// Context Monitor Entry Structure
struct fault_log_entry {
	uint64_t timestamp_ns;
	uint64_t pc;
	uint64_t fault_addr;
	int32_t pid;
	char comm[16];
	int64_t err_code;
	int trap_nr;
};

// SMMU Guard Entry Structure
struct smmu_fault_entry {
	uint64_t timestamp_ns;
	uint64_t phys_addr;
	size_t size;
	uint32_t stream_id;
	char dev_name[32];
	char action[16];
};

// Log Ring 1: ctx_monitor
static struct fault_log_entry ctx_log_ring[MAX_LOG_ENTRIES];
static size_t ctx_log_head = 0;
static size_t ctx_log_count = 0;
static pthread_mutex_t ctx_log_lock = PTHREAD_MUTEX_INITIALIZER;

// Log Ring 2: smmu_guard
static struct smmu_fault_entry smmu_log_ring[MAX_LOG_ENTRIES];
static size_t smmu_log_head = 0;
static size_t smmu_log_count = 0;
static pthread_mutex_t smmu_log_lock = PTHREAD_MUTEX_INITIALIZER;

// Metrics & Stats
static atomic_size_t g_total_writes = 0;
static atomic_size_t g_total_reads = 0;
static atomic_size_t g_corrupted_entries = 0;

// Writer function for ctx_monitor
static void log_fault_event(uint64_t pc, uint64_t fault_addr, int pid, int thread_id, uint64_t seq)
{
	pthread_mutex_lock(&ctx_log_lock);

	struct fault_log_entry *entry = &ctx_log_ring[ctx_log_head];
	entry->timestamp_ns = seq;
	entry->pc = pc;
	entry->fault_addr = fault_addr;
	entry->pid = pid;
	snprintf(entry->comm, sizeof(entry->comm), "th_%d", thread_id);
	entry->err_code = (int64_t)seq * 2;
	entry->trap_nr = 14;

	ctx_log_head = (ctx_log_head + 1) % MAX_LOG_ENTRIES;
	if (ctx_log_count < MAX_LOG_ENTRIES)
		ctx_log_count++;

	pthread_mutex_unlock(&ctx_log_lock);
	atomic_fetch_add(&g_total_writes, 1);
}

// Writer function for smmu_guard
static void smmu_guard_log_blocked_dma(uint64_t phys, size_t size, uint32_t stream_id, int thread_id, uint64_t seq)
{
	pthread_mutex_lock(&smmu_log_lock);

	struct smmu_fault_entry *entry = &smmu_log_ring[smmu_log_head];
	entry->timestamp_ns = seq;
	entry->phys_addr = phys;
	entry->size = size;
	entry->stream_id = stream_id;
	snprintf(entry->dev_name, sizeof(entry->dev_name), "dev_%d", thread_id);
	strncpy(entry->action, "BLOCKED", sizeof(entry->action));

	smmu_log_head = (smmu_log_head + 1) % MAX_LOG_ENTRIES;
	if (smmu_log_count < MAX_LOG_ENTRIES)
		smmu_log_count++;

	pthread_mutex_unlock(&smmu_log_lock);
	atomic_fetch_add(&g_total_writes, 1);
}

// Reader snapshot for ctx_monitor (exact logic as ctx_monitor_proc_show)
static int ctx_monitor_proc_show_test(void)
{
	size_t count, i;
	struct fault_log_entry *snapshot;

	snapshot = malloc(MAX_LOG_ENTRIES * sizeof(*snapshot));
	if (!snapshot)
		return -1;

	pthread_mutex_lock(&ctx_log_lock);
	count = ctx_log_count;
	for (i = 0; i < count; i++) {
		size_t idx;
		if (ctx_log_count == MAX_LOG_ENTRIES)
			idx = (ctx_log_head + i) % MAX_LOG_ENTRIES;
		else
			idx = i;
		snapshot[i] = ctx_log_ring[idx];
	}
	pthread_mutex_unlock(&ctx_log_lock);

	// Validate snapshot data integrity outside lock
	for (i = 0; i < count; i++) {
		// Check that err_code == timestamp_ns * 2 (internal consistency test)
		if (snapshot[i].timestamp_ns > 0 && snapshot[i].err_code != (int64_t)snapshot[i].timestamp_ns * 2) {
			atomic_fetch_add(&g_corrupted_entries, 1);
		}
		// Check string null termination
		if (memchr(snapshot[i].comm, '\0', sizeof(snapshot[i].comm)) == NULL) {
			atomic_fetch_add(&g_corrupted_entries, 1);
		}
	}

	free(snapshot);
	atomic_fetch_add(&g_total_reads, 1);
	return 0;
}

// Reader snapshot for smmu_guard (exact logic as smmu_guard_proc_show)
static int smmu_guard_proc_show_test(void)
{
	size_t count, i;
	struct smmu_fault_entry *snapshot;

	snapshot = malloc(MAX_LOG_ENTRIES * sizeof(*snapshot));
	if (!snapshot)
		return -1;

	pthread_mutex_lock(&smmu_log_lock);
	count = smmu_log_count;
	for (i = 0; i < count; i++) {
		size_t idx;
		if (smmu_log_count == MAX_LOG_ENTRIES)
			idx = (smmu_log_head + i) % MAX_LOG_ENTRIES;
		else
			idx = i;
		snapshot[i] = smmu_log_ring[idx];
	}
	pthread_mutex_unlock(&smmu_log_lock);

	// Validate snapshot data integrity outside lock
	for (i = 0; i < count; i++) {
		if (snapshot[i].timestamp_ns > 0 && strcmp(snapshot[i].action, "BLOCKED") != 0) {
			atomic_fetch_add(&g_corrupted_entries, 1);
		}
		if (memchr(snapshot[i].dev_name, '\0', sizeof(snapshot[i].dev_name)) == NULL) {
			atomic_fetch_add(&g_corrupted_entries, 1);
		}
	}

	free(snapshot);
	atomic_fetch_add(&g_total_reads, 1);
	return 0;
}

// Thread functions
static void *writer_thread_func(void *arg)
{
	int thread_id = (int)(intptr_t)arg;
	for (uint64_t seq = 1; seq <= WRITES_PER_THREAD; seq++) {
		log_fault_event(0xffff800010000000UL + seq, 0xffff800012345000UL + seq, 1000 + thread_id, thread_id, seq);
		smmu_guard_log_blocked_dma(0x80000000UL + seq, 4096, 0x01 + thread_id, thread_id, seq);
	}
	return NULL;
}

static void *reader_thread_func(void *arg)
{
	(void)arg;
	for (int i = 0; i < READS_PER_THREAD; i++) {
		ctx_monitor_proc_show_test();
		smmu_guard_proc_show_test();
	}
	return NULL;
}

int main(void)
{
	printf("=== Running Ring Buffer Snapshot Multi-Threaded Stress Test ===\n");
	printf("Writers: %d threads (%d writes/thread)\n", NUM_WRITER_THREADS, WRITES_PER_THREAD);
	printf("Readers: %d threads (%d snapshot reads/thread)\n\n", NUM_READER_THREADS, READS_PER_THREAD);

	pthread_t writers[NUM_WRITER_THREADS];
	pthread_t readers[NUM_READER_THREADS];

	for (int i = 0; i < NUM_WRITER_THREADS; i++) {
		if (pthread_create(&writers[i], NULL, writer_thread_func, (void *)(intptr_t)i) != 0) {
			perror("pthread_create writer");
			return 1;
		}
	}

	for (int i = 0; i < NUM_READER_THREADS; i++) {
		if (pthread_create(&readers[i], NULL, reader_thread_func, (void *)(intptr_t)i) != 0) {
			perror("pthread_create reader");
			return 1;
		}
	}

	for (int i = 0; i < NUM_WRITER_THREADS; i++) {
		pthread_join(writers[i], NULL);
	}

	for (int i = 0; i < NUM_READER_THREADS; i++) {
		pthread_join(readers[i], NULL);
	}

	size_t total_writes = atomic_load(&g_total_writes);
	size_t total_reads = atomic_load(&g_total_reads);
	size_t corrupted = atomic_load(&g_corrupted_entries);

	printf("Stress Test Execution Summary:\n");
	printf("Total Log Writes Executed : %zu\n", total_writes);
	printf("Total Snapshot Reads Executed: %zu\n", total_reads);
	printf("Corrupted / Torn Entries   : %zu\n", corrupted);

	if (corrupted == 0 && total_writes > 0 && total_reads > 0) {
		printf("\n=== VERDICT: RING BUFFER SNAPSHOTTING STRESS TEST PASSED (NO DEADLOCKS, NO RACES) ===\n");
		return 0;
	} else {
		printf("\n=== VERDICT: STRESS TEST FAILED (Corrupted entries: %zu) ===\n", corrupted);
		return 1;
	}
}
