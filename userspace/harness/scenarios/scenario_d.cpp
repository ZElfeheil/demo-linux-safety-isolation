#include "scenario_d.hpp"
#include "../../common/proc_reader.hpp"
#include <chrono>
#include <fstream>
#include <thread>

namespace safety {

auto ScenarioD::setup() -> safety::expected<void, std::string> {
    if (auto res = loader_.load("modules/ctx_monitor.ko"); !res) return res;
    if (auto res = loader_.load("modules/safety_mem.ko"); !res) return res;
    if (auto res = loader_.load("modules/mutex_threads.ko"); !res) return res;
    if (auto res = loader_.load("modules/bad_driver.ko"); !res) return res;

    std::ofstream status_file("/proc/safety_mem_status");
    if (status_file.is_open()) {
        status_file << "protect";
        status_file.flush();
    }
    return {};
}

auto ScenarioD::run() -> ScenarioResult {
    auto start_ts = std::chrono::high_resolution_clock::now();

    std::ofstream bad_driver("/proc/bad_driver_ts");
    if (bad_driver.is_open()) {
        bad_driver << "3";
        bad_driver.flush();
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    ProcReader reader("/proc/safety_mem_status");
    auto content = reader.read();

    auto end_ts = std::chrono::high_resolution_clock::now();
    uint64_t latency = std::chrono::duration_cast<std::chrono::nanoseconds>(end_ts - start_ts).count();

    if (content && (content->find("0xBAD30003") != std::string::npos ||
                    content->find("CORRUPTED") != std::string::npos ||
                    content->find("value_via_phys: 0x5AFE1234") == std::string::npos)) {
        return ScenarioResult{ScenarioStatus::Passed, {latency, latency, 0}, ""};
    }

    return ScenarioResult{ScenarioStatus::Failed, {latency, 0, 0}, "Linear map bypass did not display expected value divergence"};
}

void ScenarioD::teardown() {
    loader_.unload_all();
}

} // namespace safety
