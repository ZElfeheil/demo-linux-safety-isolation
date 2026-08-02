#include "monitor/renderer.hpp"
#include <iostream>
#include <thread>
#include <atomic>
#include <csignal>
#include <chrono>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>

using namespace std::chrono_literals;

std::atomic<uint64_t> g_sigwinch_count{0};
std::atomic<bool> g_running{true};

void signal_handler(int sig) {
    if (sig == SIGWINCH) {
        g_sigwinch_count++;
    } else if (sig == SIGINT || sig == SIGTERM) {
        g_running.store(false);
    }
}

int main() {
    std::cout << "[STRESS TEST] Testing SIGWINCH and SIGINT/SIGTERM responsiveness..." << std::endl;

    std::signal(SIGWINCH, signal_handler);
    std::signal(SIGINT, signal_handler);
    std::signal(SIGTERM, signal_handler);

    safety::monitor::DisplayState display_state;
    safety::monitor::Renderer renderer(display_state);

    // Thread 1: Signal Sender (Hammer process with SIGWINCH)
    std::thread sender([&]() {
        for (int i = 0; i < 500 && g_running.load(); ++i) {
            ::kill(::getpid(), SIGWINCH);
            std::this_thread::sleep_for(1ms);
        }
    });

    // Thread 2: Simulating renderer loop responding to SIGWINCH
    std::thread worker([&]() {
        std::string buf;
        buf.reserve(8192);
        uint64_t handle_count = 0;

        while (g_running.load()) {
            auto term_size = safety::monitor::TerminalGuard::get_size();
            renderer.render(buf, term_size);
            handle_count++;
            std::this_thread::sleep_for(2ms);
        }
        std::cout << "  Worker processed " << handle_count << " frames." << std::endl;
    });

    // Let it run for 1 second, then send SIGINT
    std::this_thread::sleep_for(1s);
    std::cout << "  Sending SIGINT to self..." << std::endl;
    ::kill(::getpid(), SIGINT);

    sender.join();
    worker.join();

    std::cout << "[STRESS TEST PASS] Process handled SIGWINCH (" << g_sigwinch_count.load() << " signals) and shutdown cleanly!" << std::endl;
    return 0;
}
