#!/usr/bin/env python3
import subprocess
import os
import sys
import json
import tempfile
import shutil

BUILD_BIN_DIR = "/Users/zeyadelfeheil/Documents/github/demo-linux-safety-isolation/build/bin"
DEVMEM_BIN = os.path.join(BUILD_BIN_DIR, "devmem")
ANALYSIS_BIN = os.path.join(BUILD_BIN_DIR, "analysis")

test_results = []

def run_test(test_id, binary, args, expected_code=None, description=""):
    cmd = [binary] + args
    try:
        proc = subprocess.run(cmd, capture_output=True, text=True, timeout=5)
        stdout = proc.stdout
        stderr = proc.stderr
        returncode = proc.returncode
    except subprocess.TimeoutExpired:
        stdout = ""
        stderr = "TIMEOUT"
        returncode = -1
    except Exception as e:
        stdout = ""
        stderr = str(e)
        returncode = -2

    passed = True
    if expected_code is not None and returncode != expected_code:
        passed = False

    result = {
        "test_id": test_id,
        "description": description,
        "cmd": " ".join(cmd),
        "returncode": returncode,
        "expected_code": expected_code,
        "passed": passed,
        "stdout": stdout,
        "stderr": stderr
    }
    test_results.append(result)
    print(f"[{'PASS' if passed else 'FAIL'}] {test_id}: {description}")
    print(f"  CMD: {' '.join(cmd)}")
    print(f"  EXIT: {returncode} (expected: {expected_code})")
    if stdout.strip():
        print(f"  STDOUT: {stdout.strip()[:200]}")
    if stderr.strip():
        print(f"  STDERR: {stderr.strip()[:200]}")
    print("-" * 60)
    return result

def main():
    print("=== STARTING EMPIRICAL TEST SUITE FOR M3 BINARIES ===\n")
    
    # -------------------------------------------------------------
    # DEVMEM TEST CASES
    # -------------------------------------------------------------
    print("--- DEVMEM TESTS ---\n")

    # 1. No arguments
    run_test("DEVMEM_01", DEVMEM_BIN, [], expected_code=1,
             description="No CLI arguments passed")

    # 2. Help flags
    run_test("DEVMEM_02A", DEVMEM_BIN, ["-h"], expected_code=0,
             description="Help flag -h")
    run_test("DEVMEM_02B", DEVMEM_BIN, ["--help"], expected_code=0,
             description="Help flag --help")
    run_test("DEVMEM_02C", DEVMEM_BIN, ["help"], expected_code=0,
             description="Help argument 'help'")

    # 3. Invalid / unknown subcommand
    run_test("DEVMEM_03A", DEVMEM_BIN, ["invalid_cmd"], expected_code=1,
             description="Unknown subcommand 'invalid_cmd'")
    run_test("DEVMEM_03B", DEVMEM_BIN, ["--invalid"], expected_code=1,
             description="Unknown option '--invalid'")

    # 4. 'read' subcommand CLI errors & edge cases
    run_test("DEVMEM_04A", DEVMEM_BIN, ["read"], expected_code=1,
             description="'read' subcommand missing address argument")
    run_test("DEVMEM_04B", DEVMEM_BIN, ["read", "0xZZZZ"], expected_code=1,
             description="'read' with invalid hex string 0xZZZZ")
    run_test("DEVMEM_04C", DEVMEM_BIN, ["read", "invalid_addr"], expected_code=1,
             description="'read' with non-hex string 'invalid_addr'")
    run_test("DEVMEM_04D", DEVMEM_BIN, ["read", "0x1000G"], expected_code=1,
             description="'read' with trailing invalid char '0x1000G'")
    run_test("DEVMEM_04E", DEVMEM_BIN, ["read", ""], expected_code=1,
             description="'read' with empty string address ''")
    run_test("DEVMEM_04F", DEVMEM_BIN, ["read", "  0x40001000  "], expected_code=1,
             description="'read' with spaces in address '  0x40001000  '")
    run_test("DEVMEM_04G", DEVMEM_BIN, ["read", "-0x1000"], expected_code=1,
             description="'read' with negative hex '-0x1000'")
    run_test("DEVMEM_04H", DEVMEM_BIN, ["read", "0xFFFFFFFFFFFFFFFF1234"], expected_code=1,
             description="'read' with 64-bit uint64_t overflow value")
    run_test("DEVMEM_04I", DEVMEM_BIN, ["read", "0x40001000"], expected_code=1,
             description="'read' valid address on system without /dev/mem (proc/dev failure)")
    run_test("DEVMEM_04J", DEVMEM_BIN, ["read", "0x40001000", "extra_arg"], expected_code=1,
             description="'read' with extra argument")

    # 5. 'write' subcommand CLI errors & edge cases
    run_test("DEVMEM_05A", DEVMEM_BIN, ["write"], expected_code=1,
             description="'write' subcommand missing address and value")
    run_test("DEVMEM_05B", DEVMEM_BIN, ["write", "0x40001000"], expected_code=1,
             description="'write' subcommand missing value argument")
    run_test("DEVMEM_05C", DEVMEM_BIN, ["write", "bad_addr", "0x12345678"], expected_code=1,
             description="'write' with bad address string")
    run_test("DEVMEM_05D", DEVMEM_BIN, ["write", "0x40001000", "bad_val"], expected_code=1,
             description="'write' with bad value string")
    run_test("DEVMEM_05E", DEVMEM_BIN, ["write", "0x40001000", ""], expected_code=1,
             description="'write' with empty value string")
    run_test("DEVMEM_05F", DEVMEM_BIN, ["write", "0x40001000", "0x100000000"], expected_code=1,
             description="'write' with 32-bit overflow value 0x100000000 (> UINT32_MAX)")
    run_test("DEVMEM_05G", DEVMEM_BIN, ["write", "0x40001000", "-0xDEAD"], expected_code=1,
             description="'write' with negative hex value '-0xDEAD'")
    run_test("DEVMEM_05H", DEVMEM_BIN, ["write", "0x40001000", "0xDEADDEAD"], expected_code=1,
             description="'write' valid address/value on system without /dev/mem")
    run_test("DEVMEM_05I", DEVMEM_BIN, ["write", "0x40001000", "0xDEADDEAD", "extra_arg"], expected_code=1,
             description="'write' with extra argument")

    # 6. 'watch' subcommand CLI errors & edge cases
    run_test("DEVMEM_06A", DEVMEM_BIN, ["watch"], expected_code=1,
             description="'watch' subcommand missing address argument")
    run_test("DEVMEM_06B", DEVMEM_BIN, ["watch", "bad_addr"], expected_code=1,
             description="'watch' with bad address string")
    run_test("DEVMEM_06C", DEVMEM_BIN, ["watch", "0x40001000", "invalid_ms"], expected_code=1,
             description="'watch' with invalid interval string 'invalid_ms'")
    run_test("DEVMEM_06D", DEVMEM_BIN, ["watch", "0x40001000", "-500"], expected_code=1,
             description="'watch' with negative interval '-500'")
    run_test("DEVMEM_06E", DEVMEM_BIN, ["watch", "0x40001000", "0"], expected_code=1,
             description="'watch' with zero interval '0'")
    run_test("DEVMEM_06F", DEVMEM_BIN, ["watch", "0x40001000", "100"], expected_code=1,
             description="'watch' valid address and interval on system without /dev/mem")

    # -------------------------------------------------------------
    # ANALYSIS TEST CASES
    # -------------------------------------------------------------
    print("\n--- ANALYSIS TESTS ---\n")

    # 1. Default run (unwritable /results path on non-root system)
    run_test("ANALYSIS_01", ANALYSIS_BIN, [], expected_code=1,
             description="Default execution trying to write to /results/comparison_table.md")

    # 2. Help flags / Unknown options
    run_test("ANALYSIS_02A", ANALYSIS_BIN, ["-h"], expected_code=1,
             description="Help flag -h")
    run_test("ANALYSIS_02B", ANALYSIS_BIN, ["--help"], expected_code=1,
             description="Help flag --help")
    run_test("ANALYSIS_02C", ANALYSIS_BIN, ["--invalid_option"], expected_code=1,
             description="Unknown option '--invalid_option'")

    # 3. Valid --output to temporary file (missing procfs files scenario)
    with tempfile.NamedTemporaryFile(suffix=".md", delete=False) as tmp:
        tmp_path = tmp.name

    res_valid = run_test("ANALYSIS_03A", ANALYSIS_BIN, ["--output", tmp_path], expected_code=0,
                         description="Valid --output to temp file with missing /proc files")
    
    # Check if generated file contains proc missing markers
    if os.path.exists(tmp_path):
        with open(tmp_path, "r") as f:
            content = f.read()
        print(f"  [CHECK] Output file size: {len(content)} bytes")
        has_missing_marker = "[PROC UNLOADED / UNREADABLE: File does not exist:" in content
        print(f"  [CHECK] Handles missing procfs files gracefully in report: {has_missing_marker}")
        os.remove(tmp_path)

    # 4. Valid --output=path syntax
    with tempfile.NamedTemporaryFile(suffix=".md", delete=False) as tmp:
        tmp_path_eq = tmp.name

    run_test("ANALYSIS_03B", ANALYSIS_BIN, [f"--output={tmp_path_eq}"], expected_code=0,
             description="Valid --output=PATH syntax")
    if os.path.exists(tmp_path_eq):
        os.remove(tmp_path_eq)

    # 5. Missing value for --output (--output as last arg)
    run_test("ANALYSIS_04A", ANALYSIS_BIN, ["--output"], expected_code=1,
             description="--output passed as last argument without path value")

    # 6. Empty --output= value
    run_test("ANALYSIS_04B", ANALYSIS_BIN, ["--output="], expected_code=1,
             description="--output= passed with empty path string")

    # 7. Unwritable output path / non-existent root dir
    run_test("ANALYSIS_05A", ANALYSIS_BIN, ["--output", "/nonexistent_root_dir_12345/report.md"], expected_code=1,
             description="Unwritable path in non-existent root directory")
    run_test("ANALYSIS_05B", ANALYSIS_BIN, ["--output", "/dev/null/report.md"], expected_code=1,
             description="Invalid directory path under /dev/null")

    # Save raw test results to json
    results_json_path = "/Users/zeyadelfeheil/Documents/github/demo-linux-safety-isolation/.agents/teamwork_preview_challenger_m3_2/test_results.json"
    with open(results_json_path, "w") as f:
        json.dump(test_results, f, indent=2)
    print(f"\nTest results saved to {results_json_path}")

if __name__ == "__main__":
    main()
