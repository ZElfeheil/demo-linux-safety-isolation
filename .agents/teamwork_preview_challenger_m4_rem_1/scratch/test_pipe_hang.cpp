#include <iostream>
#include <thread>
#include <atomic>
#include <fstream>
#include <chrono>
#include <fcntl.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>

using namespace std::chrono_literals;

int main() {
    std::cout << "[EMPIRICAL TEST] Testing blocking trace_pipe read on thread join..." << std::endl;

    const char* fifo_path = "/tmp/test_trace_fifo";
    ::unlink(fifo_path);
    ::mkfifo(fifo_path, 0666);

    std::atomic<bool> running{true};

    // Open write end (O_RDWR) so FIFO doesn't return EOF to reader, but don't write any data
    int writer_fd = ::open(fifo_path, O_RDWR);

    std::thread streamer([&]() {
        std::ifstream pipe_stream(fifo_path);
        if (pipe_stream.is_open()) {
            std::string line;
            // Simulated line 164 of userspace/monitor/main.cpp:
            while (running.load() && std::getline(pipe_stream, line)) {
                // ...
            }
        }
    });

    std::this_thread::sleep_for(200ms);
    std::cout << "  Signaling thread to stop (running = false)..." << std::endl;
    running.store(false);

    std::cout << "  Attempting streamer.join()..." << std::endl;

    std::atomic<bool> join_finished{false};
    std::thread joiner([&]() {
        streamer.join();
        join_finished.store(true);
    });

    std::this_thread::sleep_for(1s);

    if (!join_finished.load()) {
        std::cout << "[FINDING CONFIRMED] Streamer thread HANGS on join()! std::getline on blocking pipe prevents shutdown!" << std::endl;
        // Unblock writer to allow test process to exit cleanly
        ::close(writer_fd);
        ::unlink(fifo_path);
        joiner.join();
    } else {
        std::cout << "Streamer joined normally." << std::endl;
        ::close(writer_fd);
        ::unlink(fifo_path);
        joiner.join();
    }

    return 0;
}
