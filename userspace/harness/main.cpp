#include <algorithm>
#include <iostream>
#include <string>
#include <string_view>
#include <vector>
#include "interactive.hpp"
#include "module_loader.hpp"
#include "scenarios/scenario_b.hpp"
#include "scenarios/scenario_d.hpp"
#include "scenarios/scenario_e.hpp"
#include "scenarios/scenario_f.hpp"
#include "scenarios/scenario_g.hpp"

namespace {
void print_usage(std::string_view prog_name) {
    std::cout << "ARM64 Linux 6.6 Safety Isolation Demo -- Presenter Harness\n"
              << "Usage: " << prog_name << " [options]\n\n"
              << "Options:\n"
              << "  --interactive          Default mode: 4-beat presenter flow with keypress pauses\n"
              << "  --auto                 Automated mode: continuous execution (for CI / recording)\n"
              << "  --scenario <id>        Run specific scenario (B, D, E, F, G, or all). Default: all core (B, D, E, F)\n"
              << "  --start-at <id>        Resume sequence starting from scenario <id> (e.g. D)\n"
              << "  -h, --help             Display this help message\n";
}
} // namespace

// cppcheck-suppress constParameter
int main(int argc, char* argv[]) {
    bool auto_mode = false;
    std::string scenario_id = "all";
    std::string start_at_id;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--interactive") {
            auto_mode = false;
        } else if (arg == "--auto" || arg == "--clean" || arg == "--quiet") {
            auto_mode = true;
        } else if (arg == "--scenario" && i + 1 < argc) {
            scenario_id = argv[++i];
            // If running a specific scenario without --interactive, default to clean execution
            if (std::none_of(argv + 1, argv + argc, [](std::string_view a) { return a == "--interactive"; })) {
                auto_mode = true;
            }
        } else if (arg == "--start-at" && i + 1 < argc) {
            start_at_id = argv[++i];
        } else if (arg == "-h" || arg == "--help") {
            print_usage(argv[0]);
            return 0;
        }
    }

    // Setup global signal handlers for kernel module unloading on SIGINT/SIGTERM
    safety::ModuleLoader::setup_signal_handlers();

    // Auto-launch tmux session if in interactive mode and not already inside tmux
    if (!auto_mode) {
        safety::PresenterEngine::ensure_tmux_environment(scenario_id, start_at_id);
    }

    safety::PresenterEngine engine(auto_mode);

    // Instantiate scenario objects
    safety::ScenarioB sc_b;
    safety::ScenarioD sc_d;
    safety::ScenarioE sc_e;
    safety::ScenarioF sc_f;
    safety::ScenarioG sc_g;

    // Define 4-beat question slides
    safety::QuestionSlide slide_b{
        .id = "B",
        .title = "Scenario B -- Mutex + Rogue Thread",
        .setup_desc = "Thread A holds safety_mutex for 50ms window. Rogue Thread C ignores the lock.",
        .question_text = "Thread A holds safety_mutex. Thread C writes without acquiring it. Can memory be corrupted?",
        .choices = {
            {'A', "No -- Thread A holds the mutex lock", false},
            {'B', "Yes -- Mutexes rely on voluntary cooperation; Thread C ignores the lock", true},
            {'C', "Depends on thread scheduling priority", false}
        },
        .explanation = "Software locks serialize cooperative execution paths. They do not restrict physical memory access.\n"
                       "Uncooperative code (rogue drivers, compromised threads) bypass software locks completely."
    };

    safety::QuestionSlide slide_d{
        .id = "D",
        .title = "Scenario D -- DMA Linear Map Bypass",
        .setup_desc = "set_memory_ro active on vmalloc alias. bad_driver writes via phys_to_virt() linear map alias.",
        .question_text = "set_memory_ro is active on vmalloc alias. bad_driver writes via phys_to_virt(). Protected?",
        .choices = {
            {'A', "Yes -- Same physical page frame, protection applies automatically", false},
            {'B', "No -- set_memory_ro modified one virtual alias PTE only", true},
            {'C', "Depends on TLB invalidation state", false}
        },
        .explanation = "set_memory_ro() updates the PTE of the requested virtual address. The ARM64 kernel linear map retains\n"
                       "its own independent PTE mapping to the same physical page frame. Closing one virtual alias leaves the\n"
                       "linear alias (and physical DMA bus) open."
    };

    safety::QuestionSlide slide_e{
        .id = "E",
        .title = "Scenario E -- SMMU Active, CPU Write Bypasses SMMU",
        .setup_desc = "SMMUv3 is active (smmu_guard.ko loaded). bad_driver writes via CPU linear map (phys_to_virt).",
        .question_text = "SMMU is active. bad_driver writes to safety memory via CPU (not DMA). Does SMMU block it?",
        .choices = {
            {'A', "Yes -- SMMU protects all physical memory from any write", false},
            {'B', "No -- SMMU only filters bus-master DMA traffic, not CPU core writes", true},
            {'C', "Depends on SMMU stream ID configuration", false}
        },
        .explanation = "SMMUv3 sits on the system bus between peripherals and memory. It filters DMA transactions from\n"
                       "bus masters (GPU, NIC, PCIe devices). CPU cores access memory through their own MMU (TTBR1_EL1),\n"
                       "which is a completely separate translation path. SMMU never sees CPU reads/writes."
    };

    safety::QuestionSlide slide_f{
        .id = "F",
        .title = "Scenario F -- Full CTX + SMMU Isolation",
        .setup_desc = "Combines Level 2 CTX PTE protection (vmalloc + linear map) with SMMUv3 IOMMU bus protection.",
        .question_text = "SMMUv3 blocks unauthorized DMA bus access. bad_driver attempts write through CPU. Does SMMU block it?",
        .choices = {
            {'A', "Yes -- SMMU filters all physical memory writes", false},
            {'B', "No -- SMMU filters bus masters (DMA); CPU MMU filters CPU access", true},
            {'C', "Only if kernel is booted with iommu=strict", false}
        },
        .explanation = "Full hardware safety isolation requires complementary CPU MMU and SMMUv3 enforcement. The CPU MMU enforces\n"
                       "virtual translation aliases for CPU cores, while SMMUv3 enforces physical I/O streams for DMA bus masters."
    };

    safety::QuestionSlide slide_g{
        .id = "G",
        .title = "Scenario G -- Mutex Metadata Attack (Q&A)",
        .setup_desc = "Rogue thread clears safety_mutex.owner = NULL directly in RAM while Thread A holds the lock.",
        .question_text = "rogue_thread sets safety_mutex.owner = NULL directly in RAM while Thread A holds it. What happens?",
        .choices = {
            {'A', "Nothing -- Thread A's local CPU registers maintain ownership", false},
            {'B', "Mutex state is corrupted; Thread B acquires the lock concurrently", true},
            {'C', "Kernel panics immediately on detection", false}
        },
        .explanation = "Software synchronization objects are data structures stored in kernel RAM. In an unprotected memory model,\n"
                       "any kernel context can overwrite lock metadata. Hardware memory isolation (RO page tables) is necessary."
    };

    // Determine execution plan based on CLI flags
    bool active = start_at_id.empty();

    if (scenario_id == "B" || (scenario_id == "all" && (active || start_at_id == "B"))) {
        active = true;
        engine.run_scenario(sc_b, slide_b);
    }

    if (scenario_id == "D" || (scenario_id == "all" && (active || start_at_id == "D"))) {
        engine.run_scenario(sc_d, slide_d);
    }

    if (scenario_id == "E" || (scenario_id == "all" && (active || start_at_id == "E"))) {
        engine.run_scenario(sc_e, slide_e);
    }

    if (scenario_id == "F" || (scenario_id == "all" && (active || start_at_id == "F"))) {
        engine.run_scenario(sc_f, slide_f);
    }

    if (scenario_id == "G") {
        engine.run_scenario(sc_g, slide_g);
    }

    std::cout << "\n[+] Presenter Harness scenario sequence complete.\n";
    return 0;
}
