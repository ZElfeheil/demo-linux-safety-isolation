## 2026-07-31T01:01:33Z

You are an Explorer agent for Milestone 3: Userspace Infrastructure & Common Headers.
Your Working Directory is /Users/zeyadelfeheil/Documents/github/demo-linux-safety-isolation/.agents/teamwork_preview_explorer_m3_1

Task:
Investigate and produce a detailed implementation blueprint for userspace/CMakeLists.txt and userspace/common/ (scenario.hpp, proc_reader.hpp, memory_region.hpp) according to docs/implementation_plan.md.

Requirements:
- Enforce C++20 standard (CMAKE_CXX_STANDARD 20).
- scenario.hpp: C++20 concept Scenario (name(), setup(), run(), teardown()) and ScenarioResult struct.
- proc_reader.hpp: ProcReader class with RAII file handling returning std::expected<std::string, std::string> (C++ Core Guidelines R.1, I.11, E.1).
- memory_region.hpp: PhysicalMemoryView managing /dev/mem mmap via std::unique_ptr<void, MmapDeleter>, returning std::span<const std::byte> and using std::bit_cast for type conversions (C++ Core Guidelines I.11, I.13, ES.49).

Save your analysis blueprint to /Users/zeyadelfeheil/Documents/github/demo-linux-safety-isolation/.agents/teamwork_preview_explorer_m3_1/analysis.md and send your handoff report to parent.
