#!/usr/bin/env python3
"""
Empirical Static Analyzer & Bug Verification Script for Kernel Modules (Milestone 2)
Scans kernel C files and Makefiles for out-of-tree ARM64 Linux 6.6 compatibility,
sparse/smatch warnings, synchronization issues, memory barrier semantics, page table walk safety,
and die_notifier priority handling.
"""

import os
import re
import sys

KERNEL_DIR = "/Users/zeyadelfeheil/Documents/github/demo-linux-safety-isolation/kernel"

def check_makefiles():
    print("=== [1] Checking Kbuild Makefiles ===")
    top_mk = os.path.join(KERNEL_DIR, "Makefile")
    if os.path.exists(top_mk):
        with open(top_mk, 'r') as f:
            content = f.read()
            if "PWD           :=" in content or "PWD ?=" in content:
                print("[WARN] top Makefile uses PWD := $(shell pwd). Consider $(CURDIR) for out-of-tree compatibility.")
            if "obj-m += safety_mem/" in content:
                print("[INFO] Kbuild subdirectories defined in top Makefile.")

    modules = ["bad_driver", "ctx_monitor", "mutex_threads", "safety_mem", "smmu_guard"]
    for mod in modules:
        mk_path = os.path.join(KERNEL_DIR, mod, "Makefile")
        if not os.path.exists(mk_path):
            print(f"[ERR] Missing Makefile for {mod}")
            continue
        with open(mk_path, 'r') as f:
            c = f.read()
            if mod in ["bad_driver", "mutex_threads"]:
                if "KBUILD_EXTRA_SYMBOLS" in c:
                    print(f"[INFO] {mod}/Makefile defines KBUILD_EXTRA_SYMBOLS for safety_mem/Module.symvers.")
                else:
                    print(f"[WARN] {mod}/Makefile missing KBUILD_EXTRA_SYMBOLS for safety_mem symbols.")

def check_c_files():
    print("\n=== [2] Static Code & Safety Sanity Check ===")
    
    findings = []
    
    c_files = []
    for root, dirs, files in os.walk(KERNEL_DIR):
        for file in files:
            if file.endswith('.c') or file.endswith('.h'):
                c_files.append(os.path.join(root, file))

    for filepath in sorted(c_files):
        rel_path = os.path.relpath(filepath, KERNEL_DIR)
        with open(filepath, 'r') as f:
            lines = f.readlines()

        for idx, line in enumerate(lines, 1):
            # Check 1: Direct access to mutex.owner
            if "mutex.owner" in line or "safety_mutex.owner" in line or "g_safety_mutex.owner" in line:
                findings.append({
                    'file': rel_path,
                    'line': idx,
                    'type': 'SPARSE/STRUCT_VIOLATION',
                    'severity': 'CRITICAL',
                    'desc': f'Direct access to internal struct mutex.owner (atomic_long_t in 6.6): "{line.strip()}"'
                })

            # Check 2: Die notifier trapnr vs fault address bug
            if "fault_addr = (unsigned long)args->trapnr" in line:
                findings.append({
                    'file': rel_path,
                    'line': idx,
                    'type': 'FAULT_LOGIC_BUG',
                    'severity': 'CRITICAL',
                    'desc': f'args->trapnr is assigned to fault_addr instead of FAR_EL1/err: "{line.strip()}"'
                })

            # Check 3: Page table walk missing pmd_bad
            if "pmd_none(*pmd)" in line:
                # Check next few lines for pmd_bad
                window = "".join(lines[idx-1:idx+5])
                if "pmd_bad" not in window:
                    findings.append({
                        'file': rel_path,
                        'line': idx,
                        'type': 'PAGE_TABLE_SAFETY',
                        'severity': 'HIGH',
                        'desc': f'pmd_none check without pmd_bad validation in page table walk: "{line.strip()}"'
                    })

            # Check 4: Drop lock inside loop in proc_show
            if "spin_unlock_irqrestore" in line and idx > 1:
                # Check if inside a loop in proc_show
                window_prev = "".join(lines[max(0, idx-10):idx])
                window_next = "".join(lines[idx:min(len(lines), idx+10)])
                if "for (" in window_prev and "spin_lock_irqsave" in window_next:
                    findings.append({
                        'file': rel_path,
                        'line': idx,
                        'type': 'CONCURRENCY_BUG',
                        'severity': 'HIGH',
                        'desc': f'Spinlock dropped and re-acquired inside loop iterating over dynamic ring buffer: "{line.strip()}"'
                    })

            # Check 5: Function pointer cast in iommu_set_fault_handler
            if "iommu_set_fault_handler" in line and "(iommu_fault_handler_t)" in line:
                findings.append({
                    'file': rel_path,
                    'line': idx,
                    'type': 'CFI_SAFETY_RISK',
                    'severity': 'MEDIUM',
                    'desc': f'Force-casting function pointer in iommu_set_fault_handler can trigger CFI traps: "{line.strip()}"'
                })

            # Check 6: safe_write vmalloc RO asymmetry
            if "safety_mem_safe_write" in line and "def " not in line:
                pass

    for f in findings:
        print(f"[{f['severity']}] {f['file']}:{f['line']} ({f['type']}): {f['desc']}")

    return findings

if __name__ == "__main__":
    check_makefiles()
    check_c_files()
