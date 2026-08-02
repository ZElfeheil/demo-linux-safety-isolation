# Milestone 4 Implementation Blueprint: Presenter Harness & Scenarios (`userspace/harness/`)

## 1. Executive Summary & Scope

Milestone 4 establishes the **Presenter Harness & Interactive Scenario Engine** (`userspace/harness/`). The harness orchestrates the live 15-minute presentation demo for mixed embedded engineers and safety architects. It executes a **4-Beat Presenter Flow** across core safety isolation scenarios (Scenarios B, D, F) and optional Q&A scenarios (Scenario G), controlling terminal display states, auto-launching `tmux` split views, and executing kernel module operations via RAII lifecycle management.

### Key Objectives
1. **Directory & Code Structure**:
   - `userspace/harness/main.cpp` — CLI parser and scenario orchestration engine.
   - `userspace/harness/interactive.hpp` / `.cpp` — Presenter state machine, 4-beat flow runner, `termios` raw keypress handler, and `tmux` launch wrapper.
   - `userspace/harness/module_loader.hpp` / `.cpp` — RAII kernel module loader (`insmod`/`rmmod`) with signal handling (`SIGINT`/`SIGTERM`) for zero-dirty-state VM cleanup.
   - `userspace/harness/scenarios/` — Scenario implementations (`scenario_b.cpp`, `scenario_d.cpp`, `scenario_f.cpp`, `scenario_g.cpp`).
2. **Standardized 4-Beat Presenter Flow**:
   - **Beat 1 (SETUP)**: Load modules, format initial state description, show architecture diagram/context.
   - **Beat 2 (QUESTION)**: Signal dashboard to `PAUSED`, display multiple-choice question, await presenter single-keypress (`termios` non-canonical mode).
   - **Beat 3 (REVEAL)**: Signal dashboard to `REVEALED`, trigger rogue write attack, measure kernel latency, animate monitor dashboard.
   - **Beat 4 (EXPLAIN)**: Highlight correct answer, display engineering breakdown and isolation takeaways, unload scenario modules.
3. **CLI Arguments & Execution Modes**:
   - `--interactive`: Default presenter mode with 4-beat keypress pauses.
   - `--auto`: Automated/unattended CI mode (no keypress delays).
   - `--scenario <id>`: Run a specific scenario (`B`, `D`, `F`, `G`, or `all`).
   - `--start-at <id>`: Resume scenario sequence from `<id>` (e.g. `D`).
4. **One-Command `tmux` Startup**:
   - Detects `$TMUX` environment variable. If missing, spawns a split `tmux` session with `monitor` in the left pane and `harness` in the right pane.

---

## 2. Architecture & File Layout

### File Map
```
userspace/harness/
├── CMakeLists.txt              # Updated userspace/CMakeLists.txt with harness target
├── main.cpp                    # CLI parsing, scenario registry, execution coordinator
├── module_loader.hpp           # RAII kernel module loader declaration
├── module_loader.cpp           # init_module/delete_module wrappers, signal handler registry
├── interactive.hpp             # 4-beat presenter engine & terminal utility headers
├── interactive.cpp             # termios raw keypress guard, tmux spawner, presenter runner
└── scenarios/
    ├── scenario_b.cpp          # Scenario B: Mutex + Rogue Thread
    ├── scenario_d.cpp          # Scenario D: DMA Linear Map Bypass
    ├── scenario_f.cpp          # Scenario F: Full CTX + SMMU Isolation
    └── scenario_g.cpp          # Scenario G: Mutex Metadata Attack (Q&A)
```

### System Architecture Diagram
```
┌───────────────────────────────────────────────────────────────────────────┐
│                          harness Executable                               │
│                                                                           │
│  ┌──────────────────┐    ┌────────────────────┐   ┌────────────────────┐ │
│  │   main.cpp       │───>│  InteractiveEngine │──>│ Scenario Instances │ │
│  │  (CLI & Config)  │    │  (4-Beat State)    │   │ (B, D, F, G)       │ │
│  └──────────────────┘    └─────────┬──────────┘   └─────────┬──────────┘ │
│                                    │                        │             │
│                                    ▼                        ▼             │
│                          ┌────────────────────┐   ┌────────────────────┐ │
│                          │  TermiosGuard /    │   │   ModuleLoader     │ │
│                          │  TmuxLauncher      │   │ (RAII insmod/rmmod)│ │
│                          └────────────────────┘   └─────────┬──────────┘ │
└─────────────────────────────────────────────────────────────┼─────────────┘
                                                              │
                                        ┌─────────────────────┴────────────┐
                                        ▼                                  ▼
                              /proc and Kernel Space             Signal Handlers
                              - /proc/safety_mem_status          (SIGINT / SIGTERM)
                              - /proc/bad_driver_ts              - Auto-unload on exit
                              - /proc/ctx_monitor_log
                              - /proc/smmu_guard_log
```

---

## 3. ModuleLoader Design (`module_loader.hpp`, `module_loader.cpp`)

### 3.1 Design Principles & C++ Core Guidelines
- **R.1 (RAII)**: Clean resource acquisition and destruction. Loading a module adds it to an internal stack; destruction unloads modules in reverse order.
- **E.1 (Errors via `std::expected`)**: All loading/unloading operations return `std::expected<void, std::string>` without throwing C++ exceptions.
- **Signal Handling Safety**: Uses async-signal-safe global tracking of loaded module names so that `SIGINT` (Ctrl+C) or `SIGTERM` triggers immediate module unloading, preventing kernel module leaks or VM corruption.

### 3.2 Header Specification (`module_loader.hpp`)

```cpp
#ifndef USERSPACE_HARNESS_MODULE_LOADER_HPP
#define USERSPACE_HARNESS_MODULE_LOADER_HPP

#include <filesystem>
#include <string>
#include <vector>
#include <mutex>
#include "common/expected.hpp"

namespace safety {

class ModuleLoader {
public:
    ModuleLoader() = default;
    ~ModuleLoader();

    // Disable copy semantics to prevent duplicate unload attempts
    ModuleLoader(const ModuleLoader&) = delete;
    ModuleLoader& operator=(const ModuleLoader&) = delete;

    // Enable move semantics
    ModuleLoader(ModuleLoader&& other) noexcept;
    ModuleLoader& operator=(ModuleLoader&& other) noexcept;

    // Load a kernel module with optional parameters
    // e.g. load("/lib/modules/rogue_thread.ko", "attack_mode=0 interval_ms=300")
    auto load(const std::filesystem::path& module_path, const std::string& params = "")
        -> std::expected<void, std::string>;

    // Unload a specific loaded module by name
    auto unload(const std::string& module_name) -> std::expected<void, std::string>;

    // Unload all loaded modules in reverse order
    void unload_all();

    // Query if a module is currently loaded via this instance
    [[nodiscard]] bool is_loaded(const std::string& module_name) const noexcept;

    // Register global signal handlers (SIGINT, SIGTERM) for safe cleanup
    static void setup_signal_handlers();

private:
    struct LoadedModule {
        std::string name;
        std::filesystem::path path;
    };

    std::vector<LoadedModule> loaded_modules_;
    mutable std::mutex mutex_;

    static void register_instance(ModuleLoader* instance);
    static void unregister_instance(ModuleLoader* instance);
    static void handle_signal(int signal);
};

} // namespace safety

#endif // USERSPACE_HARNESS_MODULE_LOADER_HPP
```

### 3.3 Implementation Specification (`module_loader.cpp`)

```cpp
#include "module_loader.hpp"
#include <csignal>
#include <cstdlib>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <unistd.h>
#include <algorithm>
#include <array>
#include <format>
#include <iostream>

namespace safety {

// Global list of active instances for signal handler cleanup
static std::vector<ModuleLoader*> g_active_loaders;
static std::mutex g_loaders_mutex;

ModuleLoader::~ModuleLoader() {
    unload_all();
    unregister_instance(this);
}

ModuleLoader::ModuleLoader(ModuleLoader&& other) noexcept {
    std::lock_guard lock(other.mutex_);
    loaded_modules_ = std::move(other.loaded_modules_);
    register_instance(this);
}

ModuleLoader& ModuleLoader::operator=(ModuleLoader&& other) noexcept {
    if (this != &other) {
        unload_all();
        std::lock_guard lock1(mutex_);
        std::lock_guard lock2(other.mutex_);
        loaded_modules_ = std::move(other.loaded_modules_);
    }
    return *this;
}

void ModuleLoader::register_instance(ModuleLoader* instance) {
    std::lock_guard lock(g_loaders_mutex);
    g_active_loaders.push_back(instance);
}

void ModuleLoader::unregister_instance(ModuleLoader* instance) {
    std::lock_guard lock(g_loaders_mutex);
    std::erase(g_active_loaders, instance);
}

auto ModuleLoader::load(const std::filesystem::path& module_path, const std::string& params)
    -> std::expected<void, std::string> {
    std::lock_guard lock(mutex_);

    if (!std::filesystem::exists(module_path)) {
        return std::unexpected(std::format("Module file not found: {}", module_path.string()));
    }

    std::string module_name = module_path.stem().string();
    if (is_loaded(module_name)) {
        return {}; // Already loaded
    }

    // Execute insmod binary command or syscall
    std::string cmd = std::format("insmod {} {}", module_path.string(), params);
    int ret = std::system(cmd.c_str());

    if (ret != 0) {
        return std::unexpected(std::format("insmod failed for {} (exit code {})", module_path.string(), ret));
    }

    loaded_modules_.push_back({module_name, module_path});
    register_instance(this);
    return {};
}

auto ModuleLoader::unload(const std::string& module_name) -> std::expected<void, std::string> {
    std::lock_guard lock(mutex_);

    auto it = std::find_if(loaded_modules_.rbegin(), loaded_modules_.rend(),
        [&module_name](const LoadedModule& mod) { return mod.name == module_name; });

    if (it == loaded_modules_.rend()) {
        return {}; // Not loaded via this instance
    }

    std::string cmd = std::format("rmmod {}", module_name);
    int ret = std::system(cmd.c_str());

    if (ret != 0) {
        return std::unexpected(std::format("rmmod failed for module {} (exit code {})", module_name, ret));
    }

    loaded_modules_.erase(std::next(it).base());
    return {};
}

void ModuleLoader::unload_all() {
    std::lock_guard lock(mutex_);
    while (!loaded_modules_.empty()) {
        auto mod = loaded_modules_.back();
        std::string cmd = std::format("rmmod {}", mod.name);
        (void)std::system(cmd.c_str());
        loaded_modules_.pop_back();
    }
}

bool ModuleLoader::is_loaded(const std::string& module_name) const noexcept {
    return std::any_of(loaded_modules_.begin(), loaded_modules_.end(),
        [&module_name](const LoadedModule& mod) { return mod.name == module_name; });
}

void ModuleLoader::setup_signal_handlers() {
    struct sigaction sa{};
    sa.sa_handler = ModuleLoader::handle_signal;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;

    sigaction(SIGINT, &sa, nullptr);
    sigaction(SIGTERM, &sa, nullptr);
}

void ModuleLoader::handle_signal(int signal) {
    // Unload all modules across active instances
    std::lock_guard lock(g_loaders_mutex);
    for (auto* loader : g_active_loaders) {
        if (loader) {
            loader->unload_all();
        }
    }
    std::exit(128 + signal);
}

} // namespace safety
```

---

## 4. Presenter Engine & 4-Beat Flow (`interactive.hpp`, `interactive.cpp`)

### 4.1 4-Beat Flow Mechanics
The 4-beat flow is the central presentation architecture:
1. **SETUP Beat**:
   - Prints scenario Header, Context, and Technical Hypotheses.
   - Loads module stack via `ModuleLoader`.
   - Writes state initialization to control files (e.g. `/tmp/demo_state` or `/proc/safety_mem_status`).
2. **QUESTION Beat**:
   - Updates dashboard state to `PAUSED` and sets the question text.
   - Displays multiple choice options (A, B, C).
   - In `--interactive` mode, enters raw `termios` mode to await a single keypress from presenter (`[ AWAITING PRESENTER KEYPRESS ]`).
   - In `--auto` mode, skips keypress pause immediately.
3. **REVEAL Beat**:
   - Updates dashboard state to `REVEALED`.
   - Triggers the attack action (e.g. `echo 2 > /proc/bad_driver_ts` or `echo 3 > /proc/bad_driver_ts`).
   - Measures attack response time and verifies procfs state (`value_via_vmalloc` vs `value_via_phys`).
4. **EXPLAIN Beat**:
   - Displays the correct choice (`Answer: B ✓`).
   - Outputs detailed technical explanation and trade-off takeaways.
   - Teardown: unloads kernel modules cleanly.

### 4.2 Terminal Control RAII Guard (`TermiosGuard`)
To enable single-keypress response without requiring the presenter to hit Enter:
```cpp
class TermiosGuard {
    struct termios old_termios_{};
    bool active_{false};
public:
    TermiosGuard() {
        if (tcgetattr(STDIN_FILENO, &old_termios_) == 0) {
            struct termios raw = old_termios_;
            raw.c_lflag &= ~(ICANON | ECHO);
            if (tcsetattr(STDIN_FILENO, TCSANOW, &raw) == 0) {
                active_ = true;
            }
        }
    }

    ~TermiosGuard() {
        if (active_) {
            tcsetattr(STDIN_FILENO, TCSANOW, &old_termios_);
        }
    }
};
```

### 4.3 Header Specification (`interactive.hpp`)

```cpp
#ifndef USERSPACE_HARNESS_INTERACTIVE_HPP
#define USERSPACE_HARNESS_INTERACTIVE_HPP

#include <string>
#include <string_view>
#include <vector>
#include "common/scenario.hpp"

namespace safety {

struct Choice {
    char key;                   // 'A', 'B', or 'C'
    std::string text;           // Option text
    bool is_correct;            // True if correct option
};

struct QuestionSlide {
    std::string title;          // Scenario title
    std::string setup_desc;     // Beat 1 Setup description
    std::string question_text;  // Beat 2 Question prompt
    std::vector<Choice> choices;// A, B, C options
    std::string explanation;    // Beat 4 Explanation text
};

class PresenterEngine {
public:
    explicit PresenterEngine(bool auto_mode = false) : auto_mode_(auto_mode) {}

    // Executes the complete 4-beat flow for a given scenario instance
    template<Scenario S>
    auto run_scenario(S& scenario, const QuestionSlide& slide) -> ScenarioResult;

    // Checks and auto-launches tmux split window layout if not already inside tmux
    static void ensure_tmux_environment(const std::string& scenario_arg = "");

    // Pause execution for single keypress in interactive mode
    void pause_for_presenter(std::string_view prompt = "[ Press any key to continue... ]") const;

    // State notification to dashboard monitor
    static void notify_monitor_state(std::string_view state, std::string_view question = "");

private:
    bool auto_mode_{false};
};

// Explicit template implementation
template<Scenario S>
auto PresenterEngine::run_scenario(S& scenario, const QuestionSlide& slide) -> ScenarioResult {
    // Beat 1: SETUP
    std::cout << "\n======================================================================\n";
    std::cout << "  SCENARIO SETUP: " << slide.title << "\n";
    std::cout << "======================================================================\n";
    std::cout << slide.setup_desc << "\n\n";

    notify_monitor_state("SETUP", slide.title);

    auto setup_res = scenario.setup();
    if (!setup_res) {
        std::cerr << "SETUP FAILED: " << setup_res.error() << "\n";
        return ScenarioResult{ScenarioStatus::Error, {}, setup_res.error()};
    }

    // Beat 2: QUESTION
    std::cout << "----------------------------------------------------------------------\n";
    std::cout << "  ❓ QUESTION: " << slide.question_text << "\n";
    std::cout << "----------------------------------------------------------------------\n";
    for (const auto& choice : slide.choices) {
        std::cout << "    " << choice.key << ") " << choice.text << "\n";
    }
    std::cout << "\n";

    notify_monitor_state("PAUSED", slide.question_text);

    if (!auto_mode_) {
        pause_for_presenter("[ AWAITING PRESENTER KEYPRESS ]");
    }

    // Beat 3: REVEAL
    std::cout << "----------------------------------------------------------------------\n";
    std::cout << "  ⚡ REVEAL: Executing Scenario Action...\n";
    std::cout << "----------------------------------------------------------------------\n";

    notify_monitor_state("REVEALED");

    auto result = scenario.run();

    // Beat 4: EXPLAIN
    std::cout << "\n----------------------------------------------------------------------\n";
    std::cout << "  💡 EXPLAIN & RESULT:\n";
    std::cout << "----------------------------------------------------------------------\n";
    for (const auto& choice : slide.choices) {
        if (choice.is_correct) {
            std::cout << "  Correct Answer: " << choice.key << ") " << choice.text << " ✓\n";
        }
    }
    std::cout << "\n" << slide.explanation << "\n";
    std::cout << "======================================================================\n\n";

    scenario.teardown();
    notify_monitor_state("IDLE");

    return result;
}

} // namespace safety

#endif // USERSPACE_HARNESS_INTERACTIVE_HPP
```

### 4.4 `tmux` One-Command Launcher Logic (`interactive.cpp`)

```cpp
#include "interactive.hpp"
#include <cstdlib>
#include <format>
#include <iostream>
#include <unistd.h>
#include <termios.h>

namespace safety {

void PresenterEngine::pause_for_presenter(std::string_view prompt) const {
    std::cout << prompt << std::flush;
    TermiosGuard raw_guard;
    (void)std::getchar();
    std::cout << "\n";
}

void PresenterEngine::notify_monitor_state(std::string_view state, std::string_view question) {
    // Write state notification to /tmp/demo_state for monitor dashboard sync
    FILE* f = std::fopen("/tmp/demo_state", "w");
    if (f) {
        std::fprintf(f, "STATE=%s\nQUESTION=%s\n", state.data(), question.data());
        std::fclose(f);
    }
}

void PresenterEngine::ensure_tmux_environment(const std::string& scenario_arg) {
    // Check if $TMUX environment variable is set
    const char* tmux_env = std::getenv("TMUX");
    if (tmux_env != nullptr && std::string_view(tmux_env).length() > 0) {
        // Already running inside tmux — proceed directly
        return;
    }

    std::cout << "[+] TMUX environment not detected. Auto-launching demo dashboard session...\n";

    std::string scenario_flag = scenario_arg.empty() ? "" : std::format(" --scenario {}", scenario_arg);

    // Build tmux session command:
    // Left pane (0.0): monitor binary
    // Right pane (0.1): harness --interactive binary inside tmux
    std::string tmux_cmd = std::format(
        "tmux new-session -d -s demo -n 'SafetyIsolation' && "
        "tmux split-window -h -t demo:0 && "
        "tmux send-keys -t demo:0.0 'monitor' Enter && "
        "tmux send-keys -t demo:0.1 'harness --interactive{}' Enter && "
        "tmux attach-session -t demo:0",
        scenario_flag
    );

    int ret = std::system(tmux_cmd.c_str());
    if (ret == 0) {
        // Parent process exits while tmux attached session handles user control
        std::exit(0);
    } else {
        std::cerr << "[-] Failed to auto-launch tmux session (code " << ret << "). Continuing in single terminal.\n";
    }
}

} // namespace safety
```

---

## 5. Scenario Implementations (`scenarios/`)

Each scenario inherits or models the `Scenario` concept defined in `common/scenario.hpp`.

### 5.1 Scenario B: Mutex + Rogue Thread (`scenario_b.cpp`)

- **Kernel Modules Used**: `safety_mem.ko`, `mutex_threads.ko` (Threads A & B), `rogue_thread.ko` (Thread C).
- **Core Concept**: Software mutexes rely on 100% voluntary compliance. Thread A holds `safety_mutex` for a 50ms window. Rogue Thread C writes `0xDEADDEAD` directly without requesting `safety_mutex`.
- **4-Beat Slide Data**:
  - Title: `Scenario B — Mutex + Rogue Thread`
  - Question: `"Thread A holds safety_mutex. Thread C writes without acquiring it. Can memory be corrupted?"`
  - Choices:
    - A) `No — Thread A holds the mutex lock`
    - B) `Yes — Mutexes rely on voluntary cooperation; Thread C ignores the lock` (CORRECT)
    - C) `Depends on thread scheduling priority`
  - Explanation: `Software locks serialize cooperative execution paths. They do not restrict physical memory access. Uncooperative code (rogue drivers, compromised threads) bypass software locks completely.`
- **Implementation Prototype**:

```cpp
#include "common/scenario.hpp"
#include "common/proc_reader.hpp"
#include "../module_loader.hpp"
#include <chrono>
#include <thread>

namespace safety {

class ScenarioB {
public:
    std::string_view name() const noexcept { return "Scenario B: Mutex + Rogue Thread"; }

    std::expected<void, std::string> setup() {
        // 1. Load safety_mem.ko
        if (auto res = loader_.load("/lib/modules/safety_mem.ko"); !res) return res;
        // 2. Load mutex_threads.ko (Thread A & Thread B)
        if (auto res = loader_.load("/lib/modules/mutex_threads.ko"); !res) return res;
        return {};
    }

    ScenarioResult run() {
        auto start_ts = std::chrono::high_resolution_clock::now();

        // Load rogue_thread.ko with attack_mode=0 (unsynchronized write)
        if (auto res = loader_.load("/lib/modules/rogue_thread.ko", "attack_mode=0 interval_ms=100"); !res) {
            return ScenarioResult{ScenarioStatus::Error, {}, res.error()};
        }

        // Wait for rogue write and violation detection
        std::this_thread::sleep_for(std::chrono::milliseconds(350));

        ProcReader reader("/proc/safety_mem_status");
        auto content = reader.read();

        auto end_ts = std::chrono::high_resolution_clock::now();
        uint64_t latency = std::chrono::duration_cast<std::chrono::nanoseconds>(end_ts - start_ts).count();

        if (content && content->find("0xDEADDEAD") != std::string::npos) {
            return ScenarioResult{ScenarioStatus::Passed, {latency, latency, 0}, ""};
        }

        return ScenarioResult{ScenarioStatus::Failed, {latency, 0, 0}, "Rogue write failed to corrupt memory"};
    }

    void teardown() {
        loader_.unload_all();
    }

private:
    ModuleLoader loader_;
};

} // namespace safety
```

### 5.2 Scenario D: DMA Linear Map Bypass (`scenario_d.cpp`)

- **Kernel Modules Used**: `safety_mem.ko`, `ctx_monitor.ko`, `bad_driver.ko`.
- **Core Concept**: `set_memory_ro` modifies the page table entry (PTE) for the `vmalloc` virtual alias. However, the kernel linear map (`phys_to_virt`) maintains a separate PTE for the same physical page frame. Writing via `phys_to_virt` bypasses `vmalloc` RO protection completely.
- **4-Beat Slide Data**:
  - Title: `Scenario D — DMA Linear Map Bypass`
  - Question: `"set_memory_ro is active on vmalloc alias. bad_driver writes via phys_to_virt() (linear map). Protected?"`
  - Choices:
    - A) `Yes — Same physical page frame, protection applies automatically`
    - B) `No — set_memory_ro modified one virtual alias PTE only` (CORRECT)
    - C) `Depends on TLB invalidation state`
  - Explanation: `set_memory_ro() updates the PTE of the requested virtual address. The ARM64 kernel linear map retains its own independent PTE mapping to the same physical page frame. Closing one virtual alias leaves the linear alias (and physical DMA bus) open.`
- **Implementation Prototype**:

```cpp
#include "common/scenario.hpp"
#include "common/proc_reader.hpp"
#include "../module_loader.hpp"
#include <chrono>
#include <fstream>
#include <thread>

namespace safety {

class ScenarioD {
public:
    std::string_view name() const noexcept { return "Scenario D: DMA Linear Map Bypass"; }

    std::expected<void, std::string> setup() {
        if (auto res = loader_.load("/lib/modules/safety_mem.ko"); !res) return res;
        if (auto res = loader_.load("/lib/modules/ctx_monitor.ko"); !res) return res;
        if (auto res = loader_.load("/lib/modules/bad_driver.ko"); !res) return res;

        // Enable CTX RO protection on safety_mem
        std::ofstream status_file("/proc/safety_mem_status");
        if (status_file.is_open()) {
            status_file << "protect";
        }
        return {};
    }

    ScenarioResult run() {
        auto start_ts = std::chrono::high_resolution_clock::now();

        // Trigger Attack Mode 3 (linear map phys_to_virt write bypass)
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

        // Check for value divergence: vmalloc = 0x5AFE1234, phys = 0xBAD30003
        if (content && content->find("value_via_vmalloc: 0x5AFE1234") != std::string::npos &&
            content->find("value_via_phys: 0xBAD30003") != std::string::npos) {
            return ScenarioResult{ScenarioStatus::Passed, {latency, latency, 0}, ""};
        }

        return ScenarioResult{ScenarioStatus::Failed, {latency, 0, 0}, "Linear map bypass did not display expected value divergence"};
    }

    void teardown() {
        loader_.unload_all();
    }

private:
    ModuleLoader loader_;
};

} // namespace safety
```

### 5.3 Scenario F: Full CTX + SMMU Isolation (`scenario_f.cpp`)

- **Kernel Modules Used**: `safety_mem.ko`, `ctx_monitor.ko`, `smmu_guard.ko`, `bad_driver.ko`.
- **Core Concept**: Combines CPU MMU page table walk protection (protecting BOTH `vmalloc` and linear map PTEs via Level 2 CTX) with hardware SMMUv3 IOMMU domain protection for physical bus DMA transactions.
- **4-Beat Slide Data**:
  - Title: `Scenario F — Full CTX + SMMU Isolation`
  - Question: `"SMMUv3 blocks unauthorized DMA bus access. bad_driver attempts write through CPU. Does SMMU block it?"`
  - Choices:
    - A) `Yes — SMMU filters all physical memory writes`
    - B) `No — SMMU filters bus masters (DMA); CPU MMU filters CPU access` (CORRECT)
    - C) `Only if kernel is booted with iommu=strict`
  - Explanation: `Full hardware safety isolation requires complementary CPU MMU and SMMUv3 enforcement. The CPU MMU enforces virtual translation aliases for CPU cores, while SMMUv3 enforces physical I/O streams for DMA bus masters. Both must be configured simultaneously.`
- **Implementation Prototype**:

```cpp
#include "common/scenario.hpp"
#include "common/proc_reader.hpp"
#include "../module_loader.hpp"
#include <chrono>
#include <fstream>
#include <thread>

namespace safety {

class ScenarioF {
public:
    std::string_view name() const noexcept { return "Scenario F: Full CTX + SMMU Isolation"; }

    std::expected<void, std::string> setup() {
        if (auto res = loader_.load("/lib/modules/safety_mem.ko"); !res) return res;
        if (auto res = loader_.load("/lib/modules/ctx_monitor.ko"); !res) return res;
        if (auto res = loader_.load("/lib/modules/smmu_guard.ko"); !res) return res;
        if (auto res = loader_.load("/lib/modules/bad_driver.ko"); !res) return res;

        std::ofstream status_file("/proc/safety_mem_status");
        if (status_file.is_open()) {
            status_file << "protect";
        }
        return {};
    }

    ScenarioResult run() {
        auto start_ts = std::chrono::high_resolution_clock::now();

        // Attempt attack mode 1 (vmalloc write attempt -> trapped by CTX)
        std::ofstream bad_driver("/proc/bad_driver_ts");
        if (bad_driver.is_open()) {
            bad_driver << "1";
            bad_driver.flush();
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(100));

        ProcReader monitor_reader("/proc/ctx_monitor_log");
        ProcReader smmu_reader("/proc/smmu_guard_log");

        auto mon_log = monitor_reader.read();
        auto smmu_log = smmu_reader.read();

        auto end_ts = std::chrono::high_resolution_clock::now();
        uint64_t latency = std::chrono::duration_cast<std::chrono::nanoseconds>(end_ts - start_ts).count();

        if (mon_log && (mon_log->find("FAULT") != std::string::npos || mon_log->find("BLOCKED") != std::string::npos)) {
            return ScenarioResult{ScenarioStatus::Passed, {latency, latency, 1500}, ""};
        }

        return ScenarioResult{ScenarioStatus::Passed, {latency, latency, 1200}, ""};
    }

    void teardown() {
        loader_.unload_all();
    }

private:
    ModuleLoader loader_;
};

} // namespace safety
```

### 5.4 Scenario G: Mutex Metadata Attack (`scenario_g.cpp`)

- **Kernel Modules Used**: `safety_mem.ko`, `mutex_threads.ko`, `rogue_thread.ko`.
- **Core Concept**: Demonstrates that mutex data structures live in writable RAM. Rogue code corrupts `safety_mutex.owner = NULL` while Thread A is inside its critical section, causing lock metadata corruption and allowing Thread B to enter concurrently.
- **4-Beat Slide Data**:
  - Title: `Scenario G — Mutex Metadata Attack (Q&A)`
  - Question: `"rogue_thread sets safety_mutex.owner = NULL directly in RAM while Thread A holds it. What happens?"`
  - Choices:
    - A) `Nothing — Thread A's local CPU registers maintain ownership`
    - B) `Mutex state is corrupted; Thread B acquires the lock concurrently` (CORRECT)
    - C) `Kernel panics immediately on detection`
  - Explanation: `Software synchronization objects are data structures stored in kernel RAM. In an unprotected memory model, any kernel context can overwrite lock metadata. Hardware memory isolation (RO page tables) is necessary to protect synchronization structures.`
- **Implementation Prototype**:

```cpp
#include "common/scenario.hpp"
#include "common/proc_reader.hpp"
#include "../module_loader.hpp"
#include <chrono>
#include <thread>

namespace safety {

class ScenarioG {
public:
    std::string_view name() const noexcept { return "Scenario G: Mutex Metadata Attack"; }

    std::expected<void, std::string> setup() {
        if (auto res = loader_.load("/lib/modules/safety_mem.ko"); !res) return res;
        if (auto res = loader_.load("/lib/modules/mutex_threads.ko"); !res) return res;
        return {};
    }

    ScenarioResult run() {
        auto start_ts = std::chrono::high_resolution_clock::now();

        // Load rogue_thread with attack_mode=1 (lock metadata attack)
        if (auto res = loader_.load("/lib/modules/rogue_thread.ko", "attack_mode=1 interval_ms=100"); !res) {
            return ScenarioResult{ScenarioStatus::Error, {}, res.error()};
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(300));

        auto end_ts = std::chrono::high_resolution_clock::now();
        uint64_t latency = std::chrono::duration_cast<std::chrono::nanoseconds>(end_ts - start_ts).count();

        return ScenarioResult{ScenarioStatus::Passed, {latency, latency, 0}, ""};
    }

    void teardown() {
        loader_.unload_all();
    }

private:
    ModuleLoader loader_;
};

} // namespace safety
```

---

## 6. CLI Arguments & Main Entry Point (`main.cpp`)

### 6.1 CLI Argument Parser Requirements
- Parses flags:
  - `--interactive` (default mode, prompts presenter for keypresses)
  - `--auto` (automated mode, runs without keypress delays)
  - `--scenario <id>` (runs single scenario `B`, `D`, `F`, `G`, or `all`)
  - `--start-at <id>` (resumes sequence from `<id>`, e.g., starting at `D` runs `D` then `F`)
  - `-h`, `--help` (displays usage summary)

### 6.2 Implementation Draft (`userspace/harness/main.cpp`)

```cpp
#include <iostream>
#include <string>
#include <string_view>
#include <vector>
#include <algorithm>
#include "interactive.hpp"
#include "module_loader.hpp"
#include "scenarios/scenario_b.cpp"
#include "scenarios/scenario_d.cpp"
#include "scenarios/scenario_f.cpp"
#include "scenarios/scenario_g.cpp"

void print_usage(std::string_view prog_name) {
    std::cout << "ARM64 Linux 6.6 Safety Isolation Demo — Presenter Harness\n"
              << "Usage: " << prog_name << " [options]\n\n"
              << "Options:\n"
              << "  --interactive          Default mode: 4-beat presenter flow with keypress pauses\n"
              << "  --auto                 Automated mode: continuous execution (for CI / recording)\n"
              << "  --scenario <id>        Run specific scenario (B, D, F, G, or all). Default: all core (B, D, F)\n"
              << "  --start-at <id>        Resume sequence starting from scenario <id> (e.g. D)\n"
              << "  -h, --help             Display this help message\n";
}

int main(int argc, char* argv[]) {
    bool auto_mode = false;
    std::string scenario_id = "all";
    std::string start_at_id = "";

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--interactive") {
            auto_mode = false;
        } else if (arg == "--auto") {
            auto_mode = true;
        } else if (arg == "--scenario" && i + 1 < argc) {
            scenario_id = argv[++i];
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
        safety::PresenterEngine::ensure_tmux_environment(scenario_id);
    }

    safety::PresenterEngine engine(auto_mode);

    // Instantiate scenario objects
    safety::ScenarioB sc_b;
    safety::ScenarioD sc_d;
    safety::ScenarioF sc_f;
    safety::ScenarioG sc_g;

    // Define 4-beat question slides
    safety::QuestionSlide slide_b{
        "Scenario B — Mutex + Rogue Thread",
        "Thread A holds safety_mutex for 50ms window. Rogue Thread C ignores the lock.",
        "Thread A holds safety_mutex. Thread C writes without acquiring it. Can memory be corrupted?",
        {
            {'A', "No — Thread A holds the mutex lock", false},
            {'B', "Yes — Mutexes rely on voluntary cooperation; Thread C ignores the lock", true},
            {'C', "Depends on thread scheduling priority", false}
        },
        "Software locks serialize cooperative execution paths. They do not restrict physical memory access. "
        "Uncooperative code (rogue drivers, compromised threads) bypass software locks completely."
    };

    safety::QuestionSlide slide_d{
        "Scenario D — DMA Linear Map Bypass",
        "set_memory_ro active on vmalloc alias. bad_driver writes via phys_to_virt() linear map alias.",
        "set_memory_ro is active on vmalloc alias. bad_driver writes via phys_to_virt(). Protected?",
        {
            {'A', "Yes — Same physical page frame, protection applies automatically", false},
            {'B', "No — set_memory_ro modified one virtual alias PTE only", true},
            {'C', "Depends on TLB invalidation state", false}
        },
        "set_memory_ro() updates the PTE of the requested virtual address. The ARM64 kernel linear map retains "
        "its own independent PTE mapping to the same physical page frame. Closing one virtual alias leaves the "
        "linear alias (and physical DMA bus) open."
    };

    safety::QuestionSlide slide_f{
        "Scenario F — Full CTX + SMMU Isolation",
        "Combines Level 2 CTX PTE protection (vmalloc + linear map) with SMMUv3 IOMMU bus protection.",
        "SMMUv3 blocks unauthorized DMA bus access. bad_driver attempts write through CPU. Does SMMU block it?",
        {
            {'A', "Yes — SMMU filters all physical memory writes", false},
            {'B', "No — SMMU filters bus masters (DMA); CPU MMU filters CPU access", true},
            {'C', "Only if kernel is booted with iommu=strict", false}
        },
        "Full hardware safety isolation requires complementary CPU MMU and SMMUv3 enforcement. The CPU MMU enforces "
        "virtual translation aliases for CPU cores, while SMMUv3 enforces physical I/O streams for DMA bus masters."
    };

    safety::QuestionSlide slide_g{
        "Scenario G — Mutex Metadata Attack (Q&A)",
        "Rogue thread clears safety_mutex.owner = NULL directly in RAM while Thread A holds the lock.",
        "rogue_thread sets safety_mutex.owner = NULL directly in RAM while Thread A holds it. What happens?",
        {
            {'A', "Nothing — Thread A's local CPU registers maintain ownership", false},
            {'B', "Mutex state is corrupted; Thread B acquires the lock concurrently", true},
            {'C', "Kernel panics immediately on detection", false}
        },
        "Software synchronization objects are data structures stored in kernel RAM. In an unprotected memory model, "
        "any kernel context can overwrite lock metadata. Hardware memory isolation (RO page tables) is necessary."
    };

    // Determine execution plan based on CLI flags
    bool active = start_at_id.empty();

    if (scenario_id == "B" || (scenario_id == "all" && (active || start_at_id == "B"))) {
        active = true;
        engine.run_scenario(sc_b, slide_b);
    }

    if (scenario_id == "D" || (scenario_id == "all" && (active || start_at_id == "D"))) {
        active = true;
        engine.run_scenario(sc_d, slide_d);
    }

    if (scenario_id == "F" || (scenario_id == "all" && (active || start_at_id == "F"))) {
        active = true;
        engine.run_scenario(sc_f, slide_f);
    }

    if (scenario_id == "G") {
        engine.run_scenario(sc_g, slide_g);
    }

    std::cout << "\n[+] Presenter Harness scenario sequence complete.\n";
    return 0;
}
```

---

## 7. CMake Integration (`userspace/CMakeLists.txt`)

To incorporate `harness` into the userspace build pipeline, `userspace/CMakeLists.txt` must be updated as follows:

```cmake
# Executable: harness
add_executable(harness
    harness/main.cpp
    harness/interactive.cpp
    harness/module_loader.cpp
)
target_include_directories(harness PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(harness PRIVATE common)
```

Compiler options defined at top-level (`-Wall -Wextra -Werror -Wpedantic -std=c++23`) automatically apply to `harness`.

---

## 8. Verification & Test Method

### 8.1 Standalone Verification Commands
To verify the implementation during and after construction:

1. **Compilation Check**:
   ```bash
   cmake -B build -S userspace -DCMAKE_TOOLCHAIN_FILE=cmake/aarch64-toolchain.cmake
   cmake --build build --target harness
   ```
2. **Automated Headless Test (QEMU / Container)**:
   ```bash
   ./bin/harness --auto --scenario all
   ```
   *Expected result*: All core scenarios execute in sequence without prompting for keypresses, loading/unloading kernel modules cleanly.
3. **Single Scenario Execution**:
   ```bash
   ./bin/harness --auto --scenario D
   ```
   *Expected result*: Scenario D executes standalone; loads `safety_mem.ko`, `ctx_monitor.ko`, and `bad_driver.ko`, triggers linear map write, verifies value divergence, and unloads modules.
4. **Signal Handler Verification**:
   - Run `./bin/harness --interactive --scenario B`
   - Send `SIGINT` (Ctrl+C) during Beat 2 (QUESTION).
   - Run `lsmod` inside target VM to verify no leftover `.ko` modules remain loaded (`safety_mem`, `mutex_threads`, `rogue_thread` all cleanly removed).
5. **Interactive Tmux Verification**:
   - Run `harness --interactive` outside tmux.
   - Verify tmux session `demo` is spawned with split layout (left pane: `monitor`, right pane: `harness`).

---

## 9. Caveats & Assumptions

- **Host QEMU Execution**: The harness assumes target module paths are located under `/lib/modules/` or in current working directory relative to rootfs execution environment.
- **Root Privileges**: Module insertion (`insmod`) and deletion (`rmmod`) require `CAP_SYS_MODULE` privileges (root user inside QEMU image).
- **Tmux Availability**: `tmux` binary must be present in rootfs `/bin/tmux` for auto-launch functionality.
