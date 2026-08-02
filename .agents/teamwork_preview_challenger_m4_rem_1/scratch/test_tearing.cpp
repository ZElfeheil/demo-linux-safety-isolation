#include "monitor/renderer.hpp"
#include <iostream>
#include <thread>
#include <atomic>
#include <chrono>
#include <vector>

using namespace safety::monitor;
using namespace std::chrono_literals;

int main() {
    std::cout << "[EMPIRICAL TEST] Checking for snapshot tearing between mem_poller and renderer..." << std::endl;

    DisplayState state;
    Renderer renderer(state);
    std::atomic<bool> running{true};
    std::atomic<uint64_t> tear_count{0};
    std::atomic<uint64_t> total_checks{0};

    // Poller updates val_vmalloc and val_phys together to matching values (e.g. 0x1111, then 0x2222, then 0x3333...)
    std::thread poller([&]() {
        uint32_t counter = 1;
        while (running.load()) {
            uint32_t val = counter++;
            // Simulate how main.cpp parses lines in a loop:
            state.val_vmalloc.store(val);
            // Artificial tiny delay or thread yield to represent parsing time between lines
            std::this_thread::yield();
            state.val_phys.store(val);

            {
                std::lock_guard lock(state.status_mx);
                state.status_str = "PROTECTED_RO"; // Always protected
            }

            std::this_thread::sleep_for(50us);
        }
    });

    // Reader thread simulates Renderer::render_normal snapshot phase
    std::thread reader([&]() {
        std::string buf;
        while (running.load()) {
            uint32_t vm_val = state.val_vmalloc.load();
            // Yield or delay representing renderer work between reads
            uint32_t ph_val = state.val_phys.load();

            std::string status;
            {
                std::lock_guard lock(state.status_mx);
                status = state.status_str;
            }

            // Renderer logic from renderer.cpp line 125:
            // bool is_corrupt = (vm_val != ph_val) || (status == "CORRUPTED");
            bool is_corrupt = (vm_val != ph_val) || (status == "CORRUPTED");

            total_checks++;
            if (is_corrupt && status == "PROTECTED_RO") {
                // False positive corruption detected!
                tear_count++;
            }
            std::this_thread::sleep_for(10us);
        }
    });

    std::this_thread::sleep_for(1s);
    running.store(false);
    poller.join();
    reader.join();

    std::cout << "Total checks: " << total_checks.load() << std::endl;
    std::cout << "Tearing / False Positive Corruptions detected: " << tear_count.load() << std::endl;

    if (tear_count > 0) {
        std::cout << "[FINDING CONFIRMED] Snapshot tearing bug reproduced! Renderer read uncoordinated atomics causing false corruption status!" << std::endl;
    } else {
        std::cout << "No tearing detected in 1 sec run." << std::endl;
    }

    return 0;
}
