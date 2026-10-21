#include <iostream>
#include <string>
#include <string_view>
#include <vector>
#include "harness/module_loader.hpp"
#include "harness/scenario_runner.hpp"
#include "harness/scenarios/scenario_b.hpp"
#include "harness/scenarios/scenario_d.hpp"
#include "harness/scenarios/scenario_e.hpp"
#include "harness/scenarios/scenario_f.hpp"
#include "harness/scenarios/scenario_g.hpp"

namespace {
void print_usage(std::string_view prog_name) {
    std::cout << "ARM64 Linux 6.6 Safety Isolation Demo -- Presenter Harness\n"
              << "Usage: " << prog_name << " [options]\n\n"
              << "Options:\n"
              << "  --scenario <id>        Run specific scenario (B, D, E, F, G, or all). Default: all core (B, D, E, F)\n"
              << "  --start-at <id>        Start the full sequence at scenario <id> (e.g. D)\n"
              << "  -h, --help             Display this help message\n";
}
} // namespace

// cppcheck-suppress constParameter
int main(int argc, char* argv[]) {
    std::string scenario_id = "all";
    std::string start_at_id;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--scenario" && i + 1 < argc) {
            scenario_id = argv[++i];
        } else if (arg == "--start-at" && i + 1 < argc) {
            start_at_id = argv[++i];
        } else if (arg == "-h" || arg == "--help") {
            print_usage(argv[0]);
            return 0;
        }
    }

    // Setup global signal handlers for kernel module unloading on SIGINT/SIGTERM.
    safety::ModuleLoader::setup_signal_handlers();

    // Instantiate scenario objects
    safety::ScenarioB sc_b;
    safety::ScenarioD sc_d;
    safety::ScenarioE sc_e;
    safety::ScenarioF sc_f;
    safety::ScenarioG sc_g;

    // Determine execution plan based on CLI flags
    bool active = start_at_id.empty();

    if (scenario_id == "B" || (scenario_id == "all" && (active || start_at_id == "B"))) {
        active = true;
        safety::ScenarioRunner::run(sc_b, "B");
    }

    if (scenario_id == "D" || (scenario_id == "all" && (active || start_at_id == "D"))) {
        safety::ScenarioRunner::run(sc_d, "D");
    }

    if (scenario_id == "E" || (scenario_id == "all" && (active || start_at_id == "E"))) {
        safety::ScenarioRunner::run(sc_e, "E");
    }

    if (scenario_id == "F" || (scenario_id == "all" && (active || start_at_id == "F"))) {
        safety::ScenarioRunner::run(sc_f, "F");
    }

    if (scenario_id == "G") {
        safety::ScenarioRunner::run(sc_g, "G");
    }

    std::cout << "\n[+] Presenter Harness scenario sequence complete.\n";
    return 0;
}
