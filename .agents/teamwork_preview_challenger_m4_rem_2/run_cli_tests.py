#!/usr/bin/env python3
import subprocess
import os
import sys

HARNESS_BIN = "/Users/zeyadelfeheil/Documents/github/demo-linux-safety-isolation/build/bin/harness"
PROJECT_DIR = "/Users/zeyadelfeheil/Documents/github/demo-linux-safety-isolation"

def run_cmd(args, env=None):
    cmd = [HARNESS_BIN] + args
    print(f"\n--- Running: {' '.join(cmd)} ---")
    custom_env = os.environ.copy()
    if env:
        custom_env.update(env)
    # Set TMUX to dummy value so interactive mode doesn't attempt tmux spawn when testing interactive logic
    res = subprocess.run(cmd, cwd=PROJECT_DIR, capture_output=True, text=True, env=custom_env)
    print(f"Exit Code: {res.returncode}")
    print(f"STDOUT:\n{res.stdout}")
    print(f"STDERR:\n{res.stderr}")
    return res

def main():
    print("=== EMPIRICAL CLI ARGUMENT TEST HARNESS ===")
    
    # Environment with TMUX set to bypass tmux auto-launch in interactive mode
    tmux_env = {"TMUX": "/tmp/dummy_tmux"}

    # 1. --help / -h
    r_help = run_cmd(["--help"])
    assert r_help.returncode == 0
    assert "Usage:" in r_help.stdout

    # 2. --auto (runs all core: B, D, F)
    r_auto = run_cmd(["--auto"])
    assert "SCENARIO SETUP: Scenario B" in r_auto.stdout
    assert "SCENARIO SETUP: Scenario D" in r_auto.stdout
    assert "SCENARIO SETUP: Scenario F" in r_auto.stdout

    # 3. --scenario B
    r_sc_b = run_cmd(["--auto", "--scenario", "B"])
    assert "SCENARIO SETUP: Scenario B" in r_sc_b.stdout
    assert "SCENARIO SETUP: Scenario D" not in r_sc_b.stdout

    # 4. --scenario D
    r_sc_d = run_cmd(["--auto", "--scenario", "D"])
    assert "SCENARIO SETUP: Scenario D" in r_sc_d.stdout
    assert "SCENARIO SETUP: Scenario B" not in r_sc_d.stdout

    # 5. --scenario F
    r_sc_f = run_cmd(["--auto", "--scenario", "F"])
    assert "SCENARIO SETUP: Scenario F" in r_sc_f.stdout
    assert "SCENARIO SETUP: Scenario B" not in r_sc_f.stdout

    # 6. --scenario G
    r_sc_g = run_cmd(["--auto", "--scenario", "G"])
    assert "SCENARIO SETUP: Scenario G" in r_sc_g.stdout
    assert "SCENARIO SETUP: Scenario B" not in r_sc_g.stdout

    # 7. --start-at D
    r_start_d = run_cmd(["--auto", "--start-at", "D"])
    assert "SCENARIO SETUP: Scenario B" not in r_start_d.stdout
    assert "SCENARIO SETUP: Scenario D" in r_start_d.stdout
    assert "SCENARIO SETUP: Scenario F" in r_start_d.stdout

    # 8. --interactive (with dummy TMUX so it doesn't fail on system call)
    r_inter = run_cmd(["--interactive", "--scenario", "B"], env=tmux_env)

    # 9. Edge cases
    # Invalid scenario
    r_inv_sc = run_cmd(["--auto", "--scenario", "NONEXISTENT"])
    print("Invalid scenario result:", r_inv_sc.returncode)

    # Missing option value
    r_miss_val = run_cmd(["--auto", "--scenario"])
    print("Missing option value result:", r_miss_val.returncode)

    # Unknown argument
    r_unk = run_cmd(["--auto", "--unknown-flag"])
    print("Unknown flag result:", r_unk.returncode)

    print("\n[+] All CLI argument combination tests finished successfully.")

if __name__ == "__main__":
    main()
