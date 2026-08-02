#!/usr/bin/env python3
"""
Stress test script for Presenter Harness & Scenarios
"""
import subprocess
import os

HARNESS_BIN = "/Users/zeyadelfeheil/Documents/github/demo-linux-safety-isolation/build/bin/harness"
PROJECT_DIR = "/Users/zeyadelfeheil/Documents/github/demo-linux-safety-isolation"

def run_harness(args, env=None):
    cmd = [HARNESS_BIN] + args
    custom_env = os.environ.copy()
    if env:
        custom_env.update(env)
    return subprocess.run(cmd, cwd=PROJECT_DIR, capture_output=True, text=True, env=custom_env)

def main():
    print("=== ADVERSARIAL STRESS TEST SUITE ===")
    
    tests = [
        ("Default flags (interactive mode outside tmux)", ["--scenario", "B"], {"TMUX": "/tmp/dummy"}),
        ("Automated mode all core scenarios", ["--auto"], {}),
        ("Single scenario B", ["--auto", "--scenario", "B"], {}),
        ("Single scenario D", ["--auto", "--scenario", "D"], {}),
        ("Single scenario F", ["--auto", "--scenario", "F"], {}),
        ("Single scenario G", ["--auto", "--scenario", "G"], {}),
        ("Start-at D", ["--auto", "--start-at", "D"], {}),
        ("Start-at F", ["--auto", "--start-at", "F"], {}),
        ("Invalid scenario ID 'XYZ'", ["--auto", "--scenario", "XYZ"], {}),
        ("Invalid start-at ID 'INVALID'", ["--auto", "--start-at", "INVALID"], {}),
        ("Dangling flag --scenario", ["--auto", "--scenario"], {}),
        ("Dangling flag --start-at", ["--auto", "--start-at"], {}),
        ("Unrecognized CLI option --bogus", ["--auto", "--bogus"], {}),
        ("Combined flags --scenario B --start-at D", ["--auto", "--scenario", "B", "--start-at", "D"], {}),
        ("Duplicate options --scenario B --scenario D", ["--auto", "--scenario", "B", "--scenario", "D"], {}),
    ]

    for title, args, env in tests:
        res = run_harness(args, env)
        print(f"\n--- Test: {title} ---")
        print(f"Command: harness {' '.join(args)}")
        print(f"Exit Code: {res.returncode}")
        print(f"STDOUT: {res.stdout.strip()}")
        print(f"STDERR: {res.stderr.strip()}")

if __name__ == "__main__":
    main()
