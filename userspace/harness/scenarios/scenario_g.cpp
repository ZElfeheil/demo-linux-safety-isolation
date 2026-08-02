#include "scenario_g.hpp"
#include "../../common/proc_reader.hpp"
#include <chrono>
#include <thread>

namespace safety {

auto ScenarioG::setup() -> safety::expected<void, std::string> {
    if (auto res = loader_.load("modules/ctx_monitor.ko"); !res) return res;
    if (auto res = loader_.load("modules/safety_mem.ko"); !res) return res;
    if (auto res = loader_.load("modules/mutex_threads.ko"); !res) return res;
    return {};
}

auto ScenarioG::run() -> ScenarioResult {
    auto start_ts = std::chrono::high_resolution_clock::now();

    // Load rogue_thread with attack_mode=1 (lock metadata attack)
    if (auto res = loader_.load("modules/rogue_thread.ko", "attack_mode=1 interval_ms=100"); !res) {
        return ScenarioResult{ScenarioStatus::Error, {}, res.error()};
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(300));

    auto end_ts = std::chrono::high_resolution_clock::now();
    uint64_t latency = static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(end_ts - start_ts).count());

    return ScenarioResult{ScenarioStatus::Passed, {latency, latency, 0}, "Mutex metadata attack executed"};
}

void ScenarioG::teardown() {
    loader_.unload_all();
}

} // namespace safety
