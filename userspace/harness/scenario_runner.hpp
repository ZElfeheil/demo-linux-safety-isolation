#ifndef USERSPACE_HARNESS_SCENARIO_RUNNER_HPP
#define USERSPACE_HARNESS_SCENARIO_RUNNER_HPP

#include <chrono>
#include <fstream>
#include <iostream>
#include <sstream>
#include <thread>
#include "common/proc_reader.hpp"
#include "common/demo_constants.hpp"
#include <string_view>
#include "common/scenario.hpp"
#include "harness/module_loader.hpp"

namespace safety {

/** Executes a scenario lifecycle and publishes its title for the live monitor. */
class ScenarioRunner {
public:
    /**
     * @brief Execute setup, action, and teardown for one scenario.
     * @tparam S Concrete type satisfying the Scenario lifecycle contract.
     * @param scenario Scenario implementation to execute.
     * @param id Short scenario identifier displayed by the monitor.
     * @return The result returned by the scenario action, or an error on setup failure.
     */
    template<Scenario S>
    static auto run(S& scenario, std::string_view id) -> ScenarioResult {
        ModuleLoader::cleanup_on_signal();
        publish_state(id, scenario.name(), "SETUP", {}, {});

        std::cout << "[+] Running " << scenario.name() << "...\n";
        auto setup_result = scenario.setup();
        if (!setup_result) {
            std::cerr << "[-] Setup failed: " << setup_result.error() << "\n";
            return ScenarioResult{ScenarioStatus::Error, {}, setup_result.error()};
        }

        const std::string before_snapshot = read_status();
        publish_state(id, scenario.name(), "BEFORE", before_snapshot, {});
        // Give the independently running monitor time to ingest the baseline.
        std::this_thread::sleep_for(demo::kMonitorSnapshotSettleTime);

        const auto result = scenario.run();
        const std::string after_snapshot = read_status();
        publish_state(id, scenario.name(), "AFTER", before_snapshot, after_snapshot);
        std::cout << "    Result: "
                  << ((result.status == ScenarioStatus::Passed) ? "PASSED" : "VIOLATION DETECTED / BLOCKED")
                  << "\n";
        if (!result.error_message.empty()) {
            std::cout << "    Detail: " << result.error_message << "\n";
        }

        // Keep the final comparison visible before teardown removes /proc state.
        std::this_thread::sleep_for(demo::kResultDisplayTime);
        scenario.teardown();
        return result;
    }

private:
    static auto read_status() -> std::string {
        ProcReader status_reader("/proc/safety_mem_status");
        const auto status = status_reader.read();
        return status ? status.value() : std::string{};
    }

    static void write_snapshot(std::ofstream& state_file, std::string_view prefix, const std::string& snapshot) {
        std::istringstream lines(snapshot);
        std::string line;
        while (std::getline(lines, line)) {
            const auto colon = line.find(':');
            if (colon == std::string::npos) {
                continue;
            }
            state_file << prefix << line.substr(0, colon) << '=' << line.substr(colon + 1) << '\n';
        }
    }

    static void publish_state(std::string_view id, std::string_view title, std::string_view phase,
                              const std::string& before_snapshot, const std::string& after_snapshot) {
        std::ofstream state_file("/tmp/demo_state", std::ios::trunc);
        if (state_file.is_open()) {
            state_file << "SCENARIO_ID=" << id << "\n";
            state_file << "TITLE=" << title << "\n";
            state_file << "PHASE=" << phase << "\n";
            write_snapshot(state_file, "BEFORE_", before_snapshot);
            write_snapshot(state_file, "AFTER_", after_snapshot);
        }
    }
};

} // namespace safety

#endif // USERSPACE_HARNESS_SCENARIO_RUNNER_HPP
