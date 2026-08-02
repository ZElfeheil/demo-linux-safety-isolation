## 2026-07-31T07:34:06Z

You are assigned to execute Milestone 6 (Headless QEMU Automated Validation Suite & Results Comparison Delivery) for the ARM64 Linux 6.6 Safety Isolation Demonstration System.

Working Directory for metadata: /Users/zeyadelfeheil/Documents/github/demo-linux-safety-isolation/.agents/teamwork_preview_worker_m6
Project Root: /Users/zeyadelfeheil/Documents/github/demo-linux-safety-isolation

MANDATORY INTEGRITY WARNING:
DO NOT CHEAT. All implementations must be genuine. DO NOT hardcode test results, create dummy/facade implementations, or circumvent the intended task. A Forensic Auditor will independently verify your work. Integrity violations WILL be detected and your work WILL be rejected.

Your Tasks:
1. Initialize your briefing / progress files in /Users/zeyadelfeheil/Documents/github/demo-linux-safety-isolation/.agents/teamwork_preview_worker_m6.
2. Build / package all binaries and rootfs artifacts:
   - Run `docker compose run build` or `./env/build_rootfs.sh` to generate `./out/Image` and `./out/initramfs.cpio.gz`.
3. Perform automated QEMU validation execution:
   - Run `./env/run_qemu.sh` or execute `./userspace/build/bin/harness --auto --scenario all`.
   - Verify that Scenarios B (Mutex+Rogue), D (DMA Linear Map Bypass), and F (Full CTX + SMMU Isolation) pass validation assertions.
4. Generate final results report:
   - Run `./userspace/build/bin/analysis --output results/comparison_table.md` to produce `results/comparison_table.md`.
   - Verify that `results/comparison_table.md` is populated with the executive summary, tradeoff matrix, detailed scenario assessment tables, and telemetry sections matching `docs/implementation_plan.md`.
5. Create handoff.md in /Users/zeyadelfeheil/Documents/github/demo-linux-safety-isolation/.agents/teamwork_preview_worker_m6/ detailing execution logs, assertion results, and `results/comparison_table.md` contents.
6. Send a message to orchestrator with handoff path when complete.
