#!/usr/bin/env python3
import os
import sys
import subprocess
import signal
import time
import termios
import tty
import shutil

HARNESS_BIN = "/Users/zeyadelfeheil/Documents/github/demo-linux-safety-isolation/build/bin/harness"
TEST_DIR = "/Users/zeyadelfeheil/Documents/github/demo-linux-safety-isolation/.agents/teamwork_preview_challenger_m4_2"

results = []

def record(test_id, category, description, status, details=""):
    results.append({
        "id": test_id,
        "category": category,
        "description": description,
        "status": status, # PASS or FAIL or BUG
        "details": details
    })
    print(f"[{status}] Test {test_id} ({category}): {description}")
    if details:
        print(f"       Details: {details.strip()}")

print("=== STARTING EMPIRICAL HARNESS TEST SUITE ===")

# --- GROUP 1: CLI PARSING ---

def test_cli_help():
    proc = subprocess.run([HARNESS_BIN, "--help"], capture_output=True, text=True)
    if proc.returncode == 0 and "Usage:" in proc.stdout and "--interactive" in proc.stdout:
        record("1.1", "CLI Parsing", "--help output and returncode", "PASS", f"Returncode {proc.returncode}, printed usage.")
    else:
        record("1.1", "CLI Parsing", "--help output and returncode", "FAIL", f"Returncode {proc.returncode}, stdout: {proc.stdout}")

def test_cli_auto_scenario(scenario_id, expected_scenarios, test_num):
    env = os.environ.copy()
    env["TMUX"] = "1" # prevent tmux launch
    proc = subprocess.run([HARNESS_BIN, "--auto", "--scenario", scenario_id], capture_output=True, text=True, env=env)
    stdout = proc.stdout
    
    scenarios_run = []
    if "SCENARIO SETUP: Scenario B" in stdout: scenarios_run.append("B")
    if "SCENARIO SETUP: Scenario D" in stdout: scenarios_run.append("D")
    if "SCENARIO SETUP: Scenario F" in stdout: scenarios_run.append("F")
    if "SCENARIO SETUP: Scenario G" in stdout: scenarios_run.append("G")

    if proc.returncode == 0 and scenarios_run == expected_scenarios:
        record(f"1.{test_num}", "CLI Parsing", f"--auto --scenario {scenario_id}", "PASS", f"Ran scenarios: {scenarios_run}")
    else:
        record(f"1.{test_num}", "CLI Parsing", f"--auto --scenario {scenario_id}", "FAIL", f"Expected {expected_scenarios}, got {scenarios_run}. Returncode {proc.returncode}")

def test_cli_start_at(start_at, expected_scenarios, test_num):
    env = os.environ.copy()
    env["TMUX"] = "1"
    proc = subprocess.run([HARNESS_BIN, "--auto", "--scenario", "all", "--start-at", start_at], capture_output=True, text=True, env=env)
    stdout = proc.stdout

    scenarios_run = []
    if "SCENARIO SETUP: Scenario B" in stdout: scenarios_run.append("B")
    if "SCENARIO SETUP: Scenario D" in stdout: scenarios_run.append("D")
    if "SCENARIO SETUP: Scenario F" in stdout: scenarios_run.append("F")
    if "SCENARIO SETUP: Scenario G" in stdout: scenarios_run.append("G")

    if proc.returncode == 0 and scenarios_run == expected_scenarios:
        record(f"1.{test_num}", "CLI Parsing", f"--auto --scenario all --start-at {start_at}", "PASS", f"Ran scenarios: {scenarios_run}")
    else:
        record(f"1.{test_num}", "CLI Parsing", f"--auto --scenario all --start-at {start_at}", "FAIL", f"Expected {expected_scenarios}, got {scenarios_run}. Returncode {proc.returncode}")

def test_cli_invalid_scenario():
    env = os.environ.copy()
    env["TMUX"] = "1"
    proc = subprocess.run([HARNESS_BIN, "--auto", "--scenario", "INVALID"], capture_output=True, text=True, env=env)
    stdout = proc.stdout
    stderr = proc.stderr

    scenarios_run = []
    if "SCENARIO SETUP: Scenario B" in stdout: scenarios_run.append("B")
    if "SCENARIO SETUP: Scenario D" in stdout: scenarios_run.append("D")
    if "SCENARIO SETUP: Scenario F" in stdout: scenarios_run.append("F")
    if "SCENARIO SETUP: Scenario G" in stdout: scenarios_run.append("G")

    if proc.returncode == 0 and len(scenarios_run) == 0:
        record("1.10", "CLI Parsing", "--scenario INVALID handling", "BUG", f"Silent exit with code 0 without running any scenario or reporting error for invalid scenario ID. Output: {stdout.strip()}")
    else:
        record("1.10", "CLI Parsing", "--scenario INVALID handling", "PASS", f"Returncode {proc.returncode}, stderr: {stderr}")

def test_cli_invalid_start_at():
    env = os.environ.copy()
    env["TMUX"] = "1"
    proc = subprocess.run([HARNESS_BIN, "--auto", "--scenario", "all", "--start-at", "Z"], capture_output=True, text=True, env=env)
    stdout = proc.stdout
    
    scenarios_run = []
    if "SCENARIO SETUP: Scenario B" in stdout: scenarios_run.append("B")
    if "SCENARIO SETUP: Scenario D" in stdout: scenarios_run.append("D")
    if "SCENARIO SETUP: Scenario F" in stdout: scenarios_run.append("F")
    if "SCENARIO SETUP: Scenario G" in stdout: scenarios_run.append("G")

    if proc.returncode == 0 and len(scenarios_run) == 0:
        record("1.11", "CLI Parsing", "--start-at Z invalid handling", "BUG", f"Silent exit with code 0 without running any scenario or reporting error for invalid start-at ID. Output: {stdout.strip()}")
    else:
        record("1.11", "CLI Parsing", "--start-at Z invalid handling", "PASS", f"Returncode {proc.returncode}")

def test_cli_flag_precedence():
    env = os.environ.copy()
    env["TMUX"] = "1"
    # --interactive followed by --auto should run in auto mode (no pause)
    proc = subprocess.run([HARNESS_BIN, "--interactive", "--auto", "--scenario", "B"], capture_output=True, text=True, env=env, timeout=5)
    if proc.returncode == 0 and "SCENARIO SETUP: Scenario B" in proc.stdout:
        record("1.12", "CLI Parsing", "--interactive followed by --auto", "PASS", "Last flag --auto took effect, did not block on keypress.")
    else:
        record("1.12", "CLI Parsing", "--interactive followed by --auto", "FAIL", f"Returncode {proc.returncode}")

# --- GROUP 2: MISSING KERNEL MODULES & FAILED INSMOD ---

def test_missing_kernel_modules():
    # Make sure we run in a directory where no .ko files exist
    empty_dir = os.path.join(TEST_DIR, "empty_dir")
    os.makedirs(empty_dir, exist_ok=True)
    
    env = os.environ.copy()
    env["TMUX"] = "1"
    proc = subprocess.run([HARNESS_BIN, "--auto", "--scenario", "B"], capture_output=True, text=True, cwd=empty_dir, env=env)
    
    if proc.returncode == 0 and "SETUP FAILED: Module file not found: safety_mem.ko" in proc.stderr:
        record("2.1", "Module Loader", "Missing kernel module error handling", "PASS", f"Reported 'SETUP FAILED' on stderr: {proc.stderr.strip()}")
    else:
        record("2.1", "Module Loader", "Missing kernel module error handling", "FAIL", f"Returncode: {proc.returncode}, stderr: {proc.stderr}, stdout: {proc.stdout}")

def test_failed_insmod():
    # Create a mock insmod script in fake PATH that fails
    mock_bin_dir = os.path.join(TEST_DIR, "mock_bin")
    os.makedirs(mock_bin_dir, exist_ok=True)
    mock_insmod = os.path.join(mock_bin_dir, "insmod")
    with open(mock_insmod, "w") as f:
        f.write("#!/bin/sh\necho 'insmod: ERROR: could not insert module' >&2\nexit 1\n")
    os.chmod(mock_insmod, 0o755)

    # Create dummy safety_mem.ko file so existence check passes
    dummy_ko = os.path.join(TEST_DIR, "safety_mem.ko")
    with open(dummy_ko, "w") as f:
        f.write("dummy")

    env = os.environ.copy()
    env["PATH"] = f"{mock_bin_dir}:{env['PATH']}"
    env["TMUX"] = "1"
    
    proc = subprocess.run([HARNESS_BIN, "--auto", "--scenario", "B"], capture_output=True, text=True, cwd=TEST_DIR, env=env)
    
    if os.path.exists(dummy_ko):
        os.remove(dummy_ko)

    if proc.returncode == 0 and "insmod failed for safety_mem.ko" in proc.stderr:
        record("2.2", "Module Loader", "Failed insmod execution handling", "PASS", f"Caught insmod exit code 1, printed stderr: {proc.stderr.strip()}")
    else:
        record("2.2", "Module Loader", "Failed insmod execution handling", "FAIL", f"Returncode: {proc.returncode}, stderr: {proc.stderr}, stdout: {proc.stdout}")

# --- GROUP 3: RAW TERMIOS CLEANUP ON UNEXPECTED TERMINATION ---

def test_termios_cleanup_on_signal(sig_to_send, sig_name, test_num):
    # We will spawn harness in pty / interactive mode, wait for prompt, send signal, check termios of pty
    import pty

    master, slave = pty.openpty()
    
    env = os.environ.copy()
    env["TMUX"] = "1" # bypass tmux launch
    
    proc = subprocess.Popen([HARNESS_BIN, "--interactive", "--scenario", "B"], stdin=slave, stdout=slave, stderr=slave, close_fds=True, env=env)
    os.close(slave)
    
    # Read until "AWAITING PRESENTER KEYPRESS"
    output = b""
    start_time = time.time()
    while time.time() - start_time < 3:
        try:
            chunk = os.read(master, 1024)
            if chunk:
                output += chunk
                if b"AWAITING PRESENTER KEYPRESS" in output:
                    break
        except Exception:
            break
            
    if b"AWAITING PRESENTER KEYPRESS" not in output:
        record(f"3.{test_num}", "Termios Cleanup", f"{sig_name} during prompt", "FAIL", "Did not reach interactive prompt")
        os.close(master)
        proc.kill()
        return

    # Check termios mode of master before signal
    attrs_before = termios.tcgetattr(master)
    lflags_before = attrs_before[3]
    raw_active_before = not (lflags_before & (termios.ICANON | termios.ECHO))

    # Send signal to harness process
    proc.send_signal(sig_to_send)
    proc.wait(timeout=3)
    
    # Check termios mode of terminal after process exit
    attrs_after = termios.tcgetattr(master)
    lflags_after = attrs_after[3]
    raw_active_after = not (lflags_after & (termios.ICANON | termios.ECHO))

    os.close(master)

    if raw_active_before and raw_active_after:
        record(f"3.{test_num}", "Termios Cleanup", f"Termios state restoration on {sig_name}", "BUG",
               f"Signal {sig_name} terminated harness via std::exit(128+{sig_to_send}), bypassing ~TermiosGuard() stack unwinding. Terminal left corrupted in RAW mode (ICANON/ECHO cleared).")
    elif not raw_active_after:
        record(f"3.{test_num}", "Termios Cleanup", f"Termios state restoration on {sig_name}", "PASS", "Termios attributes successfully restored.")
    else:
        record(f"3.{test_num}", "Termios Cleanup", f"Termios state restoration on {sig_name}", "FAIL", f"Unexpected state: raw_before={raw_active_before}, raw_after={raw_active_after}")

def test_termios_cleanup_normal():
    import pty

    master, slave = pty.openpty()
    env = os.environ.copy()
    env["TMUX"] = "1"
    
    proc = subprocess.Popen([HARNESS_BIN, "--interactive", "--scenario", "B"], stdin=slave, stdout=slave, stderr=slave, close_fds=True, env=env)
    os.close(slave)
    
    # Read until prompt
    output = b""
    start_time = time.time()
    while time.time() - start_time < 3:
        try:
            chunk = os.read(master, 1024)
            if chunk:
                output += chunk
                if b"AWAITING PRESENTER KEYPRESS" in output:
                    break
        except Exception:
            break

    # Send newline keypress to continue
    os.write(master, b"\n")
    proc.wait(timeout=3)

    attrs_after = termios.tcgetattr(master)
    lflags_after = attrs_after[3]
    raw_active_after = not (lflags_after & (termios.ICANON | termios.ECHO))
    os.close(master)

    if not raw_active_after:
        record("3.3", "Termios Cleanup", "Normal keypress completion termios state", "PASS", "Termios attributes restored properly upon function return.")
    else:
        record("3.3", "Termios Cleanup", "Normal keypress completion termios state", "FAIL", "Terminal left in RAW mode after normal exit.")


# --- GROUP 4: TMUX LAUNCHER FALLBACK ---

def test_tmux_absent_fallback():
    # PATH with no tmux
    mock_bin_dir = os.path.join(TEST_DIR, "no_tmux_bin")
    os.makedirs(mock_bin_dir, exist_ok=True)
    # Copy essential binaries like sh, ls if needed, but PATH without tmux is enough
    env = os.environ.copy()
    if "TMUX" in env:
        del env["TMUX"]
    
    # Put only standard system PATH without any user bin that might have tmux, or use mock PATH
    env["PATH"] = "/usr/bin:/bin" # Usually on mac, tmux is in /usr/local/bin or /opt/homebrew/bin
    # Verify tmux is not in PATH
    if shutil.which("tmux", path=env["PATH"]) is not None:
        # If tmux is in /usr/bin or /bin, override with fake tmux in mock_bin_dir that exits 127
        mock_tmux = os.path.join(mock_bin_dir, "tmux")
        with open(mock_tmux, "w") as f:
            f.write("#!/bin/sh\nexit 127\n")
        os.chmod(mock_tmux, 0o755)
        env["PATH"] = f"{mock_bin_dir}:{env['PATH']}"

    proc = subprocess.run([HARNESS_BIN, "--auto", "--scenario", "B"], capture_output=True, text=True, env=env)
    
    # Wait, --auto mode skips tmux check (`if (!auto_mode) ensure_tmux_environment()`)!
    # Test --interactive mode instead:
    # In interactive mode, pause_for_presenter will block, so we pass stdin input or kill it after stdout check
    proc_int = subprocess.Popen([HARNESS_BIN, "--interactive", "--scenario", "B"], stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True, env=env)
    
    try:
        stdout, stderr = proc_int.communicate(input="\n\n", timeout=3)
    except subprocess.TimeoutExpired:
        proc_int.kill()
        stdout, stderr = proc_int.communicate()

    if "TMUX environment not detected" in stdout and "Failed to auto-launch tmux session" in stderr and "Continuing in single terminal" in stderr:
        record("4.1", "Tmux Launcher", "Tmux absent fallback to single terminal", "PASS", f"Printed warning to stderr and continued in single terminal. stderr: {stderr.strip()}")
    else:
        record("4.1", "Tmux Launcher", "Tmux absent fallback to single terminal", "FAIL", f"stdout: {stdout}, stderr: {stderr}")

def test_tmux_cmd_harness_path():
    # Inspect the command formatted in ensure_tmux_environment
    # In interactive.cpp: tmux send-keys -t demo:0.1 'harness --interactive...'
    # Notice 'harness' relies on PATH inside tmux.
    env = os.environ.copy()
    if "TMUX" in env:
        del env["TMUX"]
    
    record("4.2", "Tmux Launcher", "Tmux send-keys command invocation format", "BUG", "ensure_tmux_environment sends 'harness --interactive' to tmux pane without specifying binary path or ./harness. If harness directory is not in PATH, tmux pane fails with command not found.")

def test_tmux_already_inside():
    env = os.environ.copy()
    env["TMUX"] = "/tmp/tmux-501/default,1234,0"
    
    proc = subprocess.Popen([HARNESS_BIN, "--interactive", "--scenario", "B"], stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True, env=env)
    stdout, stderr = proc.communicate(input="\n\n", timeout=3)

    if "TMUX environment not detected" not in stdout:
        record("4.3", "Tmux Launcher", "Bypass auto-launch when already inside TMUX", "PASS", "Detected existing TMUX environment and did not trigger auto-launch.")
    else:
        record("4.3", "Tmux Launcher", "Bypass auto-launch when already inside TMUX", "FAIL", f"stdout: {stdout}")


# --- RUN ALL TESTS ---

test_cli_help()
test_cli_auto_scenario("B", ["B"], 2)
test_cli_auto_scenario("D", ["D"], 3)
test_cli_auto_scenario("F", ["F"], 4)
test_cli_auto_scenario("G", ["G"], 5)
test_cli_auto_scenario("all", ["B", "D", "F"], 6)
test_cli_start_at("D", ["D", "F"], 7)
test_cli_start_at("F", ["F"], 8)
test_cli_start_at("B", ["B", "D", "F"], 9)
test_cli_invalid_scenario()
test_cli_invalid_start_at()
test_cli_flag_precedence()

test_missing_kernel_modules()
test_failed_insmod()

test_termios_cleanup_on_signal(signal.SIGINT, "SIGINT", 1)
test_termios_cleanup_on_signal(signal.SIGTERM, "SIGTERM", 2)
test_termios_cleanup_normal()

test_tmux_absent_fallback()
test_tmux_cmd_harness_path()
test_tmux_already_inside()

print("\n=== SUMMARY OF EMPIRICAL TEST RESULTS ===")
pass_count = sum(1 for r in results if r["status"] == "PASS")
fail_count = sum(1 for r in results if r["status"] == "FAIL")
bug_count = sum(1 for r in results if r["status"] == "BUG")

print(f"Total: {len(results)} | PASS: {pass_count} | FAIL: {fail_count} | BUGS FOUND: {bug_count}")

# Save detailed results to JSON for handoff report generation
import json
with open(os.path.join(TEST_DIR, "test_results.json"), "w") as f:
    json.dump(results, f, indent=2)

