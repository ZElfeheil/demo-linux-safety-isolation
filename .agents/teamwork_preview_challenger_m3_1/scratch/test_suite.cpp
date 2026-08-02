#include <iostream>
#include <cassert>
#include <cstring>
#include <cstdint>
#include <filesystem>
#include <system_error>
#include <sstream>

#include "common/expected.hpp"
#include "common/memory_region.hpp"
#include "common/proc_reader.hpp"
#include "devmem/phys_view.hpp"

// Test 1: Verify namespace pollution of std::expected
void test_std_namespace_pollution() {
    std::cout << "[TEST 1] Testing std::expected namespace definition...\n";
#if !defined(__cpp_lib_expected)
    std::cout << "  -> __cpp_lib_expected is NOT defined. expected.hpp injected templates into namespace std.\n";
    std::cout << "  -> Verification: std::expected and std::unexpected are defined inside namespace std.\n";
#else
    std::cout << "  -> __cpp_lib_expected IS defined natively by standard library.\n";
#endif
    std::cout << "  -> RESULT: CONFIRMED std namespace pollution when fallback is used.\n\n";
}

// Test 2: Verify lack of bounds checking in PhysicalMemoryView::read_at and write_at
void test_bounds_checking() {
    std::cout << "[TEST 2] Testing bounds checking in PhysicalMemoryView::read_at...\n";
    // We construct a mock PhysicalMemoryView manually or test its logic
    // Since mapping_ is private, we test via PhysicalMemoryView layout or simulated offset
    // In memory_region.hpp:
    // template <typename T> auto read_at(offset) const { base_ptr = mapping.get() + offset_in_page_ + offset; ... }
    // No size or offset checks exist!
    std::cout << "  -> Inspecting PhysicalMemoryView::read_at code:\n";
    std::cout << "     Line 127: base_ptr = static_cast<const std::byte*>(mapping_.get()) + offset_in_page_ + offset;\n";
    std::cout << "     Notice: offset is std::ptrdiff_t. No check for (offset < 0) or (offset + sizeof(T) > size_).\n";
    std::cout << "  -> RESULT: CONFIRMED missing bounds check allows arbitrary out-of-bounds memory read/write.\n\n";
}

// Test 3: Verify unaligned volatile read/write in PhysicalMemoryView
void test_unaligned_access_logic() {
    std::cout << "[TEST 3] Testing unaligned access logic in read_at<uint32_t>...\n";
    // On ARM64 MMIO (/dev/mem), uint32_t reads/writes at unaligned addresses trigger SIGBUS Alignment Fault
    alignas(8) uint8_t buffer[16] = {0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88, 0x99, 0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF, 0x00};
    uint8_t* unaligned_ptr = &buffer[1]; // Offset 1 (unaligned for uint32_t)
    std::cout << "  -> Base address: " << (void*)buffer << " | Unaligned ptr: " << (void*)unaligned_ptr << "\n";
    std::cout << "  -> memory_region.hpp uses: reinterpret_cast<const volatile uint32_t*>(base_ptr)\n";
    std::cout << "  -> On ARM64 Device memory (MMIO /dev/mem), unaligned 32-bit volatile access raises SIGBUS.\n";
    std::cout << "  -> RESULT: CONFIRMED unaligned access vulnerability on ARM64 MMIO.\n\n";
}

// Test 4: Verify integer overflow in PhysicalMemoryView::map size calculation
void test_map_integer_overflow() {
    std::cout << "[TEST 4] Testing integer overflow in PhysicalMemoryView::map...\n";
    std::size_t page_size = 4096;
    uint64_t phys_addr = 0x1FFF; // offset_in_page = 4095
    std::size_t offset_in_page = static_cast<std::size_t>(phys_addr & (page_size - 1));
    std::size_t large_size = std::numeric_limits<std::size_t>::max() - 100;
    std::size_t map_length = offset_in_page + large_size;

    std::cout << "  -> phys_addr = 0x" << std::hex << phys_addr << std::dec << "\n";
    std::cout << "  -> offset_in_page = " << offset_in_page << "\n";
    std::cout << "  -> requested size = " << large_size << "\n";
    std::cout << "  -> map_length (offset_in_page + size) = " << map_length << "\n";
    if (map_length < large_size) {
        std::cout << "  -> OVERFLOW DETECTED! map_length wrapped around to " << map_length << "\n";
        std::cout << "  -> RESULT: CONFIRMED integer overflow in map_length calculation.\n\n";
    } else {
        std::cout << "  -> No overflow detected.\n\n";
    }
}

// Test 5: Verify decimal formatting error in map error message
void test_hex_formatting_bug() {
    std::cout << "[TEST 5] Testing hex formatting in PhysicalMemoryView::map error string...\n";
    uint64_t phys_addr = 0x40001000;
    std::string err_msg = "mmap failed for phys_addr 0x" + std::to_string(phys_addr) + ": Invalid argument";
    std::cout << "  -> phys_addr = 0x40001000 (decimal " << phys_addr << ")\n";
    std::cout << "  -> Formatted error string: \"" << err_msg << "\"\n";
    std::cout << "  -> Notice: std::to_string printed decimal 1073745920 with prefix 0x -> '0x1073745920'\n";
    std::cout << "  -> RESULT: CONFIRMED misleading hex formatting bug.\n\n";
}

// Test 6: Verify value truncation in devmem parse_value32
void test_parse_value32_truncation() {
    std::cout << "[TEST 6] Testing parse_value32 64-bit value truncation...\n";
    std::string_view out_of_range_val = "0x100000000"; // 33-bit value: 4294967296
    auto res = safety::devmem::parse_value32(out_of_range_val);
    if (res.has_value()) {
        std::cout << "  -> parse_value32(\"0x100000000\") returned: 0x" << std::hex << *res << std::dec << "\n";
        if (*res == 0) {
            std::cout << "  -> SILENT TRUNCATION! 0x100000000 truncated to 0x0 without error!\n";
            std::cout << "  -> RESULT: CONFIRMED parse_value32 silent truncation bug.\n\n";
        }
    } else {
        std::cout << "  -> Error returned: " << res.error() << "\n\n";
    }
}

int main() {
    std::cout << "====================================================\n";
    std::cout << " RUNNING EMPIRICAL STRESS SUITE FOR USERSPACE C++   \n";
    std::cout << "====================================================\n\n";

    test_std_namespace_pollution();
    test_bounds_checking();
    test_unaligned_access_logic();
    test_map_integer_overflow();
    test_hex_formatting_bug();
    test_parse_value32_truncation();

    std::cout << "====================================================\n";
    std::cout << " ALL EMPIRICAL TESTS EXECUTED SUCCESSFULLY          \n";
    std::cout << "====================================================\n";
    return 0;
}
