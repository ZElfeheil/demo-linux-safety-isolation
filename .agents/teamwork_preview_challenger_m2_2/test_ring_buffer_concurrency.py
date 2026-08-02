#!/usr/bin/env python3
"""
Empirical test harness for ring buffer iteration during concurrent writes
simulating ctx_monitor_proc_show and smmu_guard_proc_show lock-dropping behavior.
"""

import threading
import time

MAX_LOG_ENTRIES = 128

class RingBufferSimulator:
    def __init__(self):
        self.log_ring = [0] * MAX_LOG_ENTRIES
        self.log_head = 0
        self.log_count = 0
        self.lock = threading.Lock()
        self.written_sequence = []

    def log_fault_event(self, event_id: int):
        with self.lock:
            self.log_ring[self.log_head] = event_id
            self.log_head = (self.log_head + 1) % MAX_LOG_ENTRIES
            if self.log_count < MAX_LOG_ENTRIES:
                self.log_count += 1
            self.written_sequence.append(event_id)

    def proc_show_sim(self) -> list[int]:
        output = []
        with self.lock:
            count = self.log_count
            
        for i in range(count):
            with self.lock:
                current_count = self.log_count
                current_head = self.log_head
                if current_count == MAX_LOG_ENTRIES:
                    idx = (current_head + i) % MAX_LOG_ENTRIES
                else:
                    idx = i
                entry = self.log_ring[idx]
            
            # Simulate seq_printf delay outside lock
            time.sleep(0.0002)
            output.append(entry)
            
        return output

def run_stress_test():
    sim = RingBufferSimulator()
    
    # Pre-fill buffer to 128
    for id_val in range(1, 129):
        sim.log_fault_event(id_val)
        
    read_results = []
    
    def writer_thread():
        # Concurrently push 20 new events (causing buffer overrun/wrap)
        for id_val in range(129, 149):
            time.sleep(0.0005)
            sim.log_fault_event(id_val)

    def reader_thread():
        res = sim.proc_show_sim()
        read_results.extend(res)

    t_writer = threading.Thread(target=writer_thread)
    t_reader = threading.Thread(target=reader_thread)

    t_reader.start()
    t_writer.start()

    t_reader.join()
    t_writer.join()

    print(f"Total read entries: {len(read_results)}")
    
    missing = []
    for x in range(1, 129):
        if x not in read_results:
            missing.append(x)

    print(f"Missing entries from initial snapshot: {missing}")
    print(f"Read results sample: {read_results[:15]}")
    
    if missing:
        print("[!] CONFIRMED BUG: Re-reading log_head inside loop during concurrent writes causes skipped entries!")

if __name__ == "__main__":
    run_stress_test()
