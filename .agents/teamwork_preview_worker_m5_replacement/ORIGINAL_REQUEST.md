## 2026-07-31T07:30:12Z
<USER_REQUEST>
You are assigned as the replacement worker for Milestone 5 (Quality Assurance, Static Analysis & Sanitizer Suite) for the ARM64 Linux 6.6 Safety Isolation Demonstration System.

Working Directory for metadata: /Users/zeyadelfeheil/Documents/github/demo-linux-safety-isolation/.agents/teamwork_preview_worker_m5_replacement
Project Root: /Users/zeyadelfeheil/Documents/github/demo-linux-safety-isolation

MANDATORY INTEGRITY WARNING:
DO NOT CHEAT. All implementations must be genuine. DO NOT hardcode test results, create dummy/facade implementations, or circumvent the intended task. A Forensic Auditor will independently verify your work. Integrity violations WILL be detected and your work WILL be rejected.

Your Tasks:
1. Initialize your briefing / progress files in /Users/zeyadelfeheil/Documents/github/demo-linux-safety-isolation/.agents/teamwork_preview_worker_m5_replacement.
2. Kernel Static Analysis:
   - Run Kbuild static checks (`make -C kernel static-check` or `make C=1` / `sparse` / `smatch`) across kernel modules.
   - Verify zero static check errors or unannotated memory space violations.
3. C++ Static Analysis:
   - Run `clang-tidy` against `userspace/` C++ source files (`common/`, `devmem/`, `analysis/`, `monitor/`, `harness/`) adhering to `.clang-tidy`.
   - Run `cppcheck --enable=all userspace/`.
   - Verify zero errors or unsuppressed C++ Core Guidelines violations.
4. Runtime Sanitizer Suite Validation:
   - Configure ASan + UBSan build (`cmake -B userspace/build-asan -S userspace -DCMAKE_CXX_FLAGS="-fsanitize=address,undefined"` and `cmake --build userspace/build-asan`).
   - Run `bin/devmem`, `bin/analysis`, `bin/monitor`, and `bin/harness --auto --scenario all` under ASan/UBSan to verify 0 memory leaks or undefined behaviors.
   - Configure TSan build (`cmake -B userspace/build-tsan -S userspace -DCMAKE_CXX_FLAGS="-fsanitize=thread"` and `cmake --build userspace/build-tsan`).
   - Run `bin/monitor` and `bin/harness` under TSan to verify 0 data races in 3x `std::jthread` loops.
5. Create handoff.md in /Users/zeyadelfeheil/Documents/github/demo-linux-safety-isolation/.agents/teamwork_preview_worker_m5_replacement/ detailing command outputs, log snippets, and verification summary.
6. Send a message to orchestrator with handoff path when complete.
</USER_REQUEST>
