#include <iostream>
#include <fstream>
#include <filesystem>
#include <cassert>
#include "../../userspace/harness/scenarios/scenario_f.hpp"
#include "../../userspace/common/proc_reader.hpp"

namespace fs = std::filesystem;

int main() {
    std::cout << "=== EMPIRICAL TEST: Scenario F & ProcReader Verification ===\n";

    safety::ScenarioF sc_f;

    // Test 1: Absent kernel logs
    std::cout << "\n[Test 1] Executing ScenarioF::run() when /proc log files are absent...\n";
    safety::ScenarioResult res1 = sc_f.run();

    std::cout << "Status: " << (res1.status == safety::ScenarioStatus::Failed ? "FAILED (Expected)" : "OTHER") << "\n";
    std::cout << "Error Message: '" << res1.error_message << "'\n";
    
    assert(res1.status == safety::ScenarioStatus::Failed);
    assert(res1.error_message == "No CTX or SMMU fault/blocked log recorded during isolation test");
    std::cout << "-> Test 1 PASSED: Scenario F cleanly failed with expected error message when kernel logs absent.\n";

    // Test 2: ProcReader behavior on non-existent file
    std::cout << "\n[Test 2] ProcReader behavior on non-existent file...\n";
    safety::ProcReader missing_reader("/tmp/nonexistent_proc_file_12345");
    auto res_missing = missing_reader.read();
    assert(!res_missing.has_value());
    std::cout << "ProcReader returned expected error: " << res_missing.error() << "\n";
    std::cout << "-> Test 2 PASSED.\n";

    std::cout << "\n[+] All Scenario F empirical tests completed successfully.\n";
    return 0;
}
