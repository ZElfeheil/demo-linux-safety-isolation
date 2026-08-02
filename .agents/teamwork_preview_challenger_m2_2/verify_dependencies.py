#!/usr/bin/env python3
"""
Static & structural dependency verification script for kernel modules.
Parses symbol exports, symbol imports, headers, and Makefiles across kernel/.
"""

import os
import re

KERNEL_DIR = "/Users/zeyadelfeheil/Documents/github/demo-linux-safety-isolation/kernel"

MODULES = ["safety_mem", "bad_driver", "mutex_threads", "ctx_monitor", "smmu_guard"]

def analyze_symbols():
    exports = {}
    imports = {}

    for root, dirs, files in os.walk(KERNEL_DIR):
        for file in files:
            if file.endswith(".c") or file.endswith(".h"):
                path = os.path.join(root, file)
                mod_name = os.path.basename(os.path.dirname(path))
                with open(path, "r", encoding="utf-8", errors="ignore") as f:
                    content = f.read()

                # Find EXPORT_SYMBOL_GPL or EXPORT_SYMBOL
                exp_matches = re.findall(r'EXPORT_SYMBOL(?:_GPL)?\s*\(\s*([A-Za-z0-9_]+)\s*\)', content)
                for exp in exp_matches:
                    exports[exp] = (mod_name, file)

                # Find extern symbols or imported function declarations
                ext_matches = re.findall(r'extern\s+[\w\s\*]+\s+([A-Za-z0-9_]+)\s*[\(\;]', content)
                for imp in ext_matches:
                    if imp not in imports:
                        imports[imp] = []
                    imports[imp].append((mod_name, file))

    print("=== EXPORTED SYMBOLS ===")
    for sym, (mod, file) in sorted(exports.items()):
        print(f"Symbol: {sym:<35} | Module: {mod:<15} | File: {file}")

    print("\n=== IMPORTED SYMBOLS / EXTERN DECLARATIONS ===")
    for sym, implist in sorted(imports.items()):
        for mod, file in implist:
            exported_by = exports.get(sym, ("UNKNOWN/UNEXPORTED", "N/A"))
            print(f"Import: {sym:<35} | Used in: {mod:<15} ({file}) | Provider: {exported_by[0]}")

    print("\n=== DEAD EXPORTS (Exported but unused by any other module) ===")
    all_imports = set(imports.keys())
    # Also check direct function calls in C files
    for root, dirs, files in os.walk(KERNEL_DIR):
        for file in files:
            if file.endswith(".c"):
                path = os.path.join(root, file)
                mod_name = os.path.basename(os.path.dirname(path))
                with open(path, "r", encoding="utf-8", errors="ignore") as f:
                    content = f.read()
                for exp in exports:
                    if exp in content and exports[exp][0] != mod_name:
                        all_imports.add(exp)

    for exp, (mod, file) in sorted(exports.items()):
        if exp not in all_imports:
            print(f"Dead Export: {exp:<35} | Defined in: {mod:<15} ({file})")

if __name__ == "__main__":
    analyze_symbols()
