#include "scenario_e.hpp"
#include "../../common/proc_reader.hpp"
#include <chrono>
#include <fstream>
#include <thread>

namespace safety {

auto ScenarioE::setup() -> safety::expected<void, std::string> {
    // Load SMMU guard (bus protection) but do NOT protect the linear map PTE.
    // This deliberately leaves the CPU path open while the SMMU path is closed.
    if (auto res = loader_.load("modules/ctx_monitor.ko"); !res) return res;
    if (auto res = loader_.load("modules/safety_mem.ko"); !res) return res;
    if (auto res = loader_.load("modules/smmu_guard.ko"); !res) return res;
    if (auto res = loader_.load("modules/bad_driver.ko"); !res) return res;

    // Do NOT write "protect" to /proc/safety_mem_status — linear map stays writable.
    // SMMU is active (smmu_guard.ko loaded) but CPU MMU PTEs are unchanged.
    return {};
}

auto ScenarioE::run() -> ScenarioResult {
    auto start_ts = std::chrono::high_resolution_clock::now();

    // Trigger attack mode 3: write via phys_to_virt() linear map alias.
    // SMMU is active but this is a CPU write — SMMU cannot see it.
    std::ofstream bad_driver("/proc/bad_driver_ts");
    if (bad_driver.is_open()) {
        bad_driver << "3";
        bad_driver.flush();
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    // Read safety_mem_status — expect corruption via linear map despite SMMU being active
    ProcReader status_reader("/proc/safety_mem_status");
    auto content = status_reader.read();

    // Check SMMU logs — should NOT show a fault (CPU writes don't go through SMMU)
    ProcReader smmu_reader("/proc/smmu_guard_log");
    auto smmu_log = smmu_reader.read();

    auto end_ts = std::chrono::high_resolution_clock::now();
    uint64_t latency = std::chrono::duration_cast<std::chrono::nanoseconds>(end_ts - start_ts).count();

    bool memory_corrupted = content && (content->find("0xBAD30003") != std::string::npos ||
                                         content->find("CORRUPTED") != std::string::npos);
    bool smmu_silent = !smmu_log || smmu_log->find("FAULT") == std::string::npos;

    // PASS = memory IS corrupted AND SMMU did NOT fire a fault
    // This proves: SMMU does not block CPU writes.
    if (memory_corrupted && smmu_silent) {
        return ScenarioResult{ScenarioStatus::Passed, {latency, latency, 0},
            "CPU write via linear map SUCCEEDED despite SMMU being active. SMMU only guards bus masters."};
    }

    if (!memory_corrupted) {
        return ScenarioResult{ScenarioStatus::Failed, {latency, 0, 0},
            "Expected memory corruption via CPU linear map write, but memory was not corrupted"};
    }

    return ScenarioResult{ScenarioStatus::Failed, {latency, 0, 0},
        "SMMU unexpectedly fired a fault on a CPU write — this should not happen"};
}

void ScenarioE::teardown() {
    loader_.unload_all();
}

} // namespace safety
