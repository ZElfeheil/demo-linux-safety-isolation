#!/usr/bin/env python3
"""
Empirical test harness for procfs read/write buffer handling and bounds checking
simulating safety_mem.c and bad_driver.c procfs handlers.
"""

import sys

def simulate_safety_mem_proc_write(input_bytes: bytes) -> tuple[int, str]:
    # safety_mem.c:
    # char kbuf[16] = {0};
    # if (count >= sizeof(kbuf)) return -EINVAL;
    # if (copy_from_user(kbuf, buf, count)) return -EFAULT;
    # if (strncmp(kbuf, "1", 1) == 0 || strncmp(kbuf, "protect", 7) == 0) safety_mem_set_protection(true);
    # else if (strncmp(kbuf, "0", 1) == 0 || strncmp(kbuf, "unprotect", 9) == 0) safety_mem_set_protection(false);
    
    count = len(input_bytes)
    if count >= 16:
        return -22, "REJECTED_EINVAL (count >= 16)"
    
    # Simulate zeroed buffer copy
    kbuf = bytearray(16)
    kbuf[:count] = input_bytes
    
    # Convert to string for strncmp simulation
    # strncmp compares up to N chars
    def strncmp(s1: bytearray, s2: str, n: int) -> int:
        b2 = s2.encode('ascii')
        sub1 = s1[:n]
        sub2 = b2[:n]
        if sub1 == sub2:
            return 0
        return 1 if sub1 > sub2 else -1

    action = "NO_ACTION"
    if strncmp(kbuf, "1", 1) == 0 or strncmp(kbuf, "protect", 7) == 0:
        action = "PROTECT_TRUE"
    elif strncmp(kbuf, "0", 1) == 0 or strncmp(kbuf, "unprotect", 9) == 0:
        action = "PROTECT_FALSE"
        
    return count, action


def simulate_bad_driver_proc_write(input_bytes: bytes) -> tuple[int, str]:
    # bad_driver.c:
    # char kbuf[16] = {0};
    # if (count >= sizeof(kbuf)) return -EINVAL;
    # if (copy_from_user(kbuf, buf, count)) return -EFAULT;
    # if (strncmp(kbuf, "1", 1) == 0) execute_attack_mode_1();
    # else if (strncmp(kbuf, "2", 1) == 0) execute_attack_mode_2();
    # else if (strncmp(kbuf, "3", 1) == 0) execute_attack_mode_3();

    count = len(input_bytes)
    if count >= 16:
        return -22, "REJECTED_EINVAL (count >= 16)"

    kbuf = bytearray(16)
    kbuf[:count] = input_bytes

    def strncmp(s1: bytearray, s2: str, n: int) -> int:
        b2 = s2.encode('ascii')
        return 0 if s1[:n] == b2[:n] else 1

    action = "NO_ACTION"
    if strncmp(kbuf, "1", 1) == 0:
        action = "ATTACK_MODE_1"
    elif strncmp(kbuf, "2", 1) == 0:
        action = "ATTACK_MODE_2"
    elif strncmp(kbuf, "3", 1) == 0:
        action = "ATTACK_MODE_3"

    return count, action

def run_tests():
    print("=== Testing /proc/safety_mem_status Parsing ===")
    test_cases_safety = [
        (b"1", "Standard write '1'"),
        (b"0", "Standard write '0'"),
        (b"protect", "Standard write 'protect'"),
        (b"unprotect", "Standard write 'unprotect'"),
        (b"1\n", "Echo write '1\\n'"),
        (b"012345", "Ambiguous write '012345' (Starts with 0)"),
        (b"100000", "Ambiguous write '100000' (Starts with 1)"),
        (b"unprotected_mode", "Overlong 'unprotected_mode' (16 bytes)"),
        (b"protect_everything_now", "Overlong input (22 bytes)"),
        (b"p", "Single char 'p' (not matching protect)"),
    ]

    for inp, desc in test_cases_safety:
        ret, action = simulate_safety_mem_proc_write(inp)
        print(f"Input: {inp!r:<25} | Desc: {desc:<45} | Ret: {ret:<3} | Action: {action}")

    print("\n=== Testing /proc/bad_driver_ts Parsing ===")
    test_cases_bad = [
        (b"1", "Mode 1 trigger"),
        (b"2", "Mode 2 trigger"),
        (b"3", "Mode 3 trigger"),
        (b"10", "Misleading input '10'"),
        (b"25", "Misleading input '25'"),
        (b"39999", "Misleading input '39999'"),
        (b"1\n", "Echo input '1\\n'"),
        (b"1234567890123456", "Overlong input (16 bytes)"),
    ]

    for inp, desc in test_cases_bad:
        ret, action = simulate_bad_driver_proc_write(inp)
        print(f"Input: {inp!r:<25} | Desc: {desc:<45} | Ret: {ret:<3} | Action: {action}")

if __name__ == "__main__":
    run_tests()
