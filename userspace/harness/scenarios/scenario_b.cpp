#include "scenario_b.hpp"
#include "../../common/proc_reader.hpp"
#include <chrono>
#include <thread>

namespace safety {

auto ScenarioB::setup() -> safety::expected<void, std::string> {
    if (auto res = loader_.load("/lib/modules/safety_mem.ko"); !res) return res;
    if (auto res = loader_.load("/lib/modules/mutex_threads.ko"); !res) return res;
    return {};
}

auto ScenarioB::run() -> ScenarioResult {
    auto start_ts = std::chrono::high_resolution_clock::now();

    if (auto res = loader_.load("/lib/modules/rogue_thread.ko", "attack_mode=0 interval_ms=100"); !res) {
        return ScenarioResult{ScenarioStatus::Error, {}, res.error()};
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(300));

    ProcReader reader("/proc/safety_mem_status");
    auto content = reader.read();

    auto end_ts = std::chrono::high_resolution_clock::now();
    uint64_t latency = std::chrono::duration_cast<std::chrono::nanoseconds>(end_ts - start_ts).count();

    if (content && (content->find("0xDEADDEAD") != std::string::npos || content->find("CORRUPTED") != std::string::npos)) {
        return ScenarioResult{ScenarioStatus::Passed, {latency, latency, 0}, ""};
    }

    return ScenarioResult{ScenarioStatus::Failed, {latency, 0, 0}, "Rogue write failed to corrupt memory"};
}

void ScenarioB::teardown() {
    loader_.unload_all();
}

} // namespace safety
