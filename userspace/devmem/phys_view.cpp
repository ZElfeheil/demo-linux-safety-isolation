#include "devmem/phys_view.hpp"
#include "common/memory_region.hpp"
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <string>
#include <thread>

namespace safety::devmem {

using safety::common::PhysicalMemoryView;
using safety::common::MemoryAccessMode;

/** Parse a hexadecimal or decimal physical address from CLI text. */
auto parse_address(std::string_view str) -> safety::expected<uint64_t, std::string> {
    if (str.empty()) {
        return safety::unexpected(std::string("Empty address string"));
    }
    char* endptr = nullptr;
    std::string s(str);
    uint64_t val = std::strtoull(s.c_str(), &endptr, 0);
    if (endptr == s.c_str() || *endptr != '\0') {
        return safety::unexpected("Invalid physical address: " + s);
    }
    return val;
}

/** Parse a 32-bit value from CLI text. */
auto parse_value32(std::string_view str) -> safety::expected<uint32_t, std::string> {
    if (str.empty()) {
        return safety::unexpected(std::string("Empty value string"));
    }
    char* endptr = nullptr;
    std::string s(str);
    uint64_t val = std::strtoull(s.c_str(), &endptr, 0);
    if (endptr == s.c_str() || *endptr != '\0') {
        return safety::unexpected("Invalid 32-bit hex value: " + s);
    }
    return static_cast<uint32_t>(val);
}

/** Map and read one 32-bit value from physical memory. */
auto read_phys(uint64_t phys_addr) -> safety::expected<uint32_t, std::string> {
    auto view_res = PhysicalMemoryView::map(phys_addr, sizeof(uint32_t), MemoryAccessMode::ReadOnly);
    if (!view_res) {
        return safety::unexpected(view_res.error());
    }
    return view_res->read_at<uint32_t>(0);
}

/** Map and write one 32-bit value to physical memory. */
auto write_phys(uint64_t phys_addr, uint32_t value) -> safety::expected<void, std::string> {
    auto view_res = PhysicalMemoryView::map(phys_addr, sizeof(uint32_t), MemoryAccessMode::ReadWrite);
    if (!view_res) {
        return safety::unexpected(view_res.error());
    }
    view_res->write_at<uint32_t>(value, 0);
    return {};
}

/** Continuously display a physical 32-bit value until interrupted. */
auto watch_phys(uint64_t phys_addr) -> safety::expected<void, std::string> {
    static std::atomic<bool> dummy_stop{false};
    WatchOptions opts{.phys_addr = phys_addr, .interval = kDefaultWatchInterval, .single_shot = false};
    return watch_phys_32(opts, dummy_stop);
}

/** Public 32-bit physical-read adapter used by the CLI. */
auto read_phys_32(uint64_t phys_addr) -> safety::expected<uint32_t, std::string> {
    return read_phys(phys_addr);
}

/** Public 32-bit physical-write adapter used by the CLI. */
auto write_phys_32(uint64_t phys_addr, uint32_t value) -> safety::expected<void, std::string> {
    return write_phys(phys_addr, value);
}

/** Poll a physical 32-bit value using configurable watch options. */
auto watch_phys_32(const WatchOptions& options, const std::atomic<bool>& stop_requested) -> safety::expected<void, std::string> {
    auto view_res = PhysicalMemoryView::map(options.phys_addr, sizeof(uint32_t), MemoryAccessMode::ReadOnly);
    if (!view_res) {
        return safety::unexpected(view_res.error());
    }

    std::cout << "Watching physical address 0x" << std::hex << std::uppercase << options.phys_addr
              << " (" << std::dec << options.interval.count()
              << "ms interval, Ctrl+C to exit)..." << std::endl;

    auto last_val = view_res->read_at<uint32_t>(0);
    std::cout << "[INIT] 0x" << std::hex << std::uppercase << options.phys_addr
              << ": 0x" << std::setw(8) << std::setfill('0') << last_val << std::dec << std::endl;

    while (!stop_requested.load()) {
        std::this_thread::sleep_for(options.interval);
        auto current_val = view_res->read_at<uint32_t>(0);
        if (current_val != last_val) {
            std::cout << "\033[1;31m[CHANGED]\033[0m 0x" << std::hex << std::uppercase << options.phys_addr
                      << ": 0x" << std::setw(8) << std::setfill('0') << last_val
                      << " -> 0x" << std::setw(8) << std::setfill('0') << current_val
                      << std::dec << std::endl;
            last_val = current_val;
        }
        if (options.single_shot) {
            break;
        }
    }

    return {};
}

} // namespace safety::devmem
