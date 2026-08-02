#include "monitor/renderer.hpp"
#include <iostream>
#include <thread>
#include <vector>
#include <atomic>
#include <chrono>
#include <cassert>

using namespace safety::monitor;
using namespace std::chrono_literals;

int main() {
    std::cout << "[STRESS TEST] Starting Concurrency Safety Test..." << std::endl;

    DisplayState state;
    Renderer renderer(state);
    std::atomic<bool> running{true};
    std::atomic<uint64_t> render_count{0};
    std::atomic<uint64_t> poller_count{0};
    std::atomic<uint64_t> streamer_count{0};

    // Thread 1: High-frequency mem_poller simulation
    std::thread poller([&]() {
        uint32_t val = 0;
        while (running.load()) {
            val++;
            state.val_vmalloc.store(0x5AFE0000 + val);
            state.val_phys.store(0x5AFE0000 + val);
            state.virt_addr.store(0xffff800000000000ULL + val);
            state.phys_addr.store(0x40000000ULL + val);
            state.ctx_protected.store((val % 2) == 0);
            state.smmu_active.store((val % 3) == 0);

            {
                std::lock_guard lock(state.status_mx);
                state.mutex_owner = (val % 2 == 0) ? "kernel_driver" : "none";
            }
            {
                std::lock_guard lock(state.status_mx);
                state.status_str = (val % 5 == 0) ? "CORRUPTED" : "PROTECTED_RO";
            }

            poller_count++;
            std::this_thread::sleep_for(100us);
        }
    });

    // Thread 2: High-frequency event_streamer simulation
    std::thread streamer([&]() {
        uint64_t idx = 0;
        while (running.load()) {
            idx++;
            state.mode.store((idx % 3 == 0) ? DashboardMode::Normal : 
                             ((idx % 3 == 1) ? DashboardMode::Paused : DashboardMode::Revealed));

            {
                std::lock_guard lock(state.scenario_mx);
                state.scenario_info.id = "Scenario " + std::to_string(idx % 10);
                state.scenario_info.title = "Title Test " + std::to_string(idx);
                state.scenario_info.setup_text = "Setup text details line " + std::to_string(idx);
                state.scenario_info.question_text = "Question " + std::to_string(idx);
                state.scenario_info.options = {"A) Option 1", "B) Option 2", "C) Option 3"};
                state.scenario_info.correct_option = "A";
                state.scenario_info.explanation = "Explanation line 1\nExplanation line 2";
            }

            {
                std::lock_guard lock(state.events_mx);
                EventEntry ev{
                    .timestamp = "+0.0" + std::to_string(idx % 100) + "s",
                    .source = "test_src_" + std::to_string(idx % 5),
                    .message = "Event message stream item " + std::to_string(idx),
                    .is_warning = (idx % 4 == 0),
                    .is_error = (idx % 7 == 0)
                };
                state.events.push_back(ev);
                if (state.events.size() > state.max_events) {
                    state.events.erase(state.events.begin());
                }
            }

            streamer_count++;
            std::this_thread::sleep_for(150us);
        }
    });

    // Thread 3 & 4: Concurrent renderers
    auto render_loop = [&](int id) {
        std::string buf;
        buf.reserve(8192);
        TerminalSize sizes[] = {{80, 24}, {40, 12}, {200, 60}, {20, 10}};
        int s_idx = 0;

        while (running.load()) {
            renderer.render(buf, sizes[s_idx % 4]);
            s_idx++;
            render_count++;
            assert(!buf.empty());
            std::this_thread::sleep_for(200us);
        }
    };

    std::thread renderer_thread1(render_loop, 1);
    std::thread renderer_thread2(render_loop, 2);

    // Let stress test run for 2 seconds
    std::this_thread::sleep_for(2s);
    running.store(false);

    poller.join();
    streamer.join();
    renderer_thread1.join();
    renderer_thread2.join();

    std::cout << "[STRESS TEST PASS] Completed without crash or hang!" << std::endl;
    std::cout << "  Renders completed: " << render_count.load() << std::endl;
    std::cout << "  Poller updates:    " << poller_count.load() << std::endl;
    std::cout << "  Streamer updates:  " << streamer_count.load() << std::endl;

    return 0;
}
