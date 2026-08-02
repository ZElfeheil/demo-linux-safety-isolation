/*
 * safety_mem_toctou_test.c
 *
 * Empirical verification harness for safety_mem.c lines 225-232:
 * TOCTOU & dangling pointer dereference on mutex.owner
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <stdbool.h>
#include <unistd.h>
#include <stdatomic.h>

#define TRACK_SIZE 4096

typedef struct {
    int pid;
    char comm[16];
} task_struct_mock_t;

typedef struct {
    bool locked;
    task_struct_mock_t *owner;
} mutex_mock_t;

static mutex_mock_t g_safety_mutex = { false, NULL };
static void *freed_pointers[TRACK_SIZE];
static atomic_size_t freed_count = 0;
static pthread_mutex_t freed_track_lock = PTHREAD_MUTEX_INITIALIZER;

static atomic_long uaf_detections = 0;
static atomic_long successful_reads = 0;
static atomic_bool running = true;

bool is_pointer_freed(void *ptr) {
    if (!ptr) return false;
    pthread_mutex_lock(&freed_track_lock);
    size_t count = atomic_load(&freed_count);
    for (size_t i = 0; i < count; i++) {
        if (freed_pointers[i] == ptr) {
            pthread_mutex_unlock(&freed_track_lock);
            return true;
        }
    }
    pthread_mutex_unlock(&freed_track_lock);
    return false;
}

void mark_pointer_freed(void *ptr) {
    pthread_mutex_lock(&freed_track_lock);
    size_t count = atomic_load(&freed_count);
    if (count < TRACK_SIZE) {
        freed_pointers[count] = ptr;
        atomic_store(&freed_count, count + 1);
    }
    pthread_mutex_unlock(&freed_track_lock);
}

void *owner_thread_fn(void *arg) {
    while (atomic_load(&running)) {
        task_struct_mock_t *task = malloc(sizeof(task_struct_mock_t));
        task->pid = rand() % 10000 + 100;
        snprintf(task->comm, sizeof(task->comm), "task_%d", task->pid);

        g_safety_mutex.owner = task;
        g_safety_mutex.locked = true;

        usleep(10); // Hold mutex briefly

        // Task unlocks and exits
        g_safety_mutex.locked = false;
        g_safety_mutex.owner = NULL;

        mark_pointer_freed((void *)task);
        free(task);

        usleep(20);
    }
    return NULL;
}

// Reproduction of safety_mem_proc_show logic (lines 225-232 of safety_mem.c)
void *proc_show_thread_fn(void *arg) {
    while (atomic_load(&running)) {
        char owner_buf[16] = "none";

        // Line 225: if (mutex_is_locked(&g_safety_mutex))
        if (g_safety_mutex.locked) {
            // Line 226: struct task_struct *owner = READ_ONCE(g_safety_mutex.owner);
            task_struct_mock_t *owner = g_safety_mutex.owner;

            // Preemption window before get_task_comm
            usleep(15);

            // Line 228-229: if (owner) get_task_comm(owner_buf, owner);
            if (owner) {
                if (is_pointer_freed((void *)owner)) {
                    atomic_fetch_add(&uaf_detections, 1);
                } else {
                    snprintf(owner_buf, sizeof(owner_buf), "%s", owner->comm);
                    atomic_fetch_add(&successful_reads, 1);
                }
            }
        }
        usleep(5);
    }
    return NULL;
}

int main() {
    printf("[*] Starting safety_mem procfs mutex_owner TOCTOU / UAF Harness...\n");

    pthread_t owner_thread, proc_thread;
    pthread_create(&owner_thread, NULL, owner_thread_fn, NULL);
    pthread_create(&proc_thread, NULL, proc_show_thread_fn, NULL);

    sleep(2);
    atomic_store(&running, false);

    pthread_join(owner_thread, NULL);
    pthread_join(proc_thread, NULL);

    printf("[+] Successful owner reads: %ld\n", atomic_load(&successful_reads));
    printf("[!] Use-After-Free (freed task dereference) detections: %ld\n", atomic_load(&uaf_detections));

    if (atomic_load(&uaf_detections) > 0) {
        printf("[CONFIRMED BUG] Unlocked dereference of g_safety_mutex.owner in procfs show causes TOCTOU Use-After-Free race condition!\n");
        return 0;
    } else {
        printf("[-] Bug not reproduced.\n");
        return 1;
    }
}
