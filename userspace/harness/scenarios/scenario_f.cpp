#include "harness/scenarios/scenario_f.hpp"
#include "common/proc_reader.hpp"
#include "common/demo_constants.hpp"
#include <chrono>
#include <fstream>
#include <thread>

namespace safety {

auto ScenarioF::setup() -> safety::expected<void, std::string> {
    if (auto res = loader_.load("modules/ctx_monitor.ko"); !res) return res;
    if (auto res = loader_.load("modules/safety_mem.ko"); !res) return res;
    if (auto res = loader_.load("modules/smmu_guard.ko"); !res) return res;
    if (auto res = loader_.load("modules/mutex_threads.ko"); !res) return res;
    if (auto res = loader_.load("modules/bad_driver.ko"); !res) return res;

    std::ofstream status_file("/proc/safety_mem_status");
    if (status_file.is_open()) {
        status_file << demo::kEnableProtectionCommand;
        status_file.flush();
    }

    ProcReader status_reader("/proc/safety_mem_status");
    const auto status = status_reader.read();
    const bool target_mapping_is_ro = status &&
        status->find(demo::kProtectionEnabledStatus) != std::string::npos &&
        status->find(demo::kVmallocReadOnlyStatus) != std::string::npos;
    if (!target_mapping_is_ro) {
        return safety::unexpected<std::string>{
            "Scenario F setup aborted: vmalloc attack target was not verified read-only"};
    }
    return {};
}

auto ScenarioF::run() -> ScenarioResult {
    auto start_ts = std::chrono::high_resolution_clock::now();

    std::ofstream bad_driver("/proc/bad_driver_ts");
    if (bad_driver.is_open()) {
        bad_driver << demo::kVmallocAttackCommand;
        bad_driver.flush();
    }

    std::this_thread::sleep_for(demo::kAttackSettleTime);

    ProcReader monitor_reader("/proc/ctx_monitor_log");
    ProcReader smmu_reader("/proc/smmu_guard_log");

    auto mon_log = monitor_reader.read();
    auto smmu_log = smmu_reader.read();
    ProcReader status_reader("/proc/safety_mem_status");
    auto status = status_reader.read();

    auto end_ts = std::chrono::high_resolution_clock::now();
    uint64_t latency = std::chrono::duration_cast<std::chrono::nanoseconds>(end_ts - start_ts).count();

    const bool value_preserved = status &&
        status->find(std::string("value_via_vmalloc: ") +
                     std::string(demo::kSafetySentinelStatus)) != std::string::npos &&
        status->find(std::string("value_via_phys: ") +
                     std::string(demo::kSafetySentinelStatus)) != std::string::npos;
    if (value_preserved) {
        const bool protection_fault_recorded =
            (mon_log && (mon_log->find("FAULT") != std::string::npos ||
                         mon_log->find("BLOCKED") != std::string::npos)) ||
            (smmu_log && (smmu_log->find("FAULT") != std::string::npos ||
                          smmu_log->find("BLOCKED") != std::string::npos));
        const std::string evidence = protection_fault_recorded
            ? "Attack blocked and recorded by a protection monitor"
            : "Attack target remained read-only; safety value was preserved";
        return ScenarioResult{ScenarioStatus::Passed,
                              {latency, latency, demo::kIllustrativeTlbFlushLatencyNs}, evidence};
    }

    return ScenarioResult{
        ScenarioStatus::Failed,
        {latency, 0, 0},
        "Safety value changed after a write to the verified read-only attack target"
    };
}

void ScenarioF::teardown() {
    loader_.unload_all();
}

} // namespace safety
