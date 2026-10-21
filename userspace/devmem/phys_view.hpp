#ifndef SAFETY_DEVMEM_PHYS_VIEW_HPP
#define SAFETY_DEVMEM_PHYS_VIEW_HPP

#include "common/memory_region.hpp"
#include "common/expected.hpp"
#include <atomic>
#include <chrono>
#include <cstdint>
#include <string>
#include <string_view>

namespace safety::devmem {

inline constexpr std::chrono::milliseconds kDefaultWatchInterval{100};

/** Options controlling repeated reads of a physical 32-bit location. */
struct WatchOptions {
    uint64_t phys_addr{0};
    std::chrono::milliseconds interval{kDefaultWatchInterval};
    bool single_shot{false};
};

/** Parse a user-provided physical address. */
[[nodiscard]] auto parse_address(std::string_view str) -> safety::expected<uint64_t, std::string>;
/** Parse a user-provided 32-bit value. */
[[nodiscard]] auto parse_value32(std::string_view str) -> safety::expected<uint32_t, std::string>;

/** Read one 32-bit value from physical memory. */
[[nodiscard]] auto read_phys(uint64_t phys_addr) -> safety::expected<uint32_t, std::string>;
/** Write one 32-bit value to physical memory. */
[[nodiscard]] auto write_phys(uint64_t phys_addr, uint32_t value) -> safety::expected<void, std::string>;
/** Watch one physical address using the default polling interval. */
[[nodiscard]] auto watch_phys(uint64_t phys_addr) -> safety::expected<void, std::string>;

/** Read one 32-bit physical value through the public adapter. */
[[nodiscard]] auto read_phys_32(uint64_t phys_addr) -> safety::expected<uint32_t, std::string>;
/** Write one 32-bit physical value through the public adapter. */
[[nodiscard]] auto write_phys_32(uint64_t phys_addr, uint32_t value) -> safety::expected<void, std::string>;
/** Watch a physical value until the caller requests termination. */
[[nodiscard]] auto watch_phys_32(const WatchOptions& options, const std::atomic<bool>& stop_requested) -> safety::expected<void, std::string>;

} // namespace safety::devmem

#endif // SAFETY_DEVMEM_PHYS_VIEW_HPP
