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

struct WatchOptions {
    uint64_t phys_addr{0};
    std::chrono::milliseconds interval{100};
    bool single_shot{false};
};

[[nodiscard]] auto parse_address(std::string_view str) -> safety::expected<uint64_t, std::string>;
[[nodiscard]] auto parse_value32(std::string_view str) -> safety::expected<uint32_t, std::string>;

[[nodiscard]] auto read_phys(uint64_t phys_addr) -> safety::expected<uint32_t, std::string>;
[[nodiscard]] auto write_phys(uint64_t phys_addr, uint32_t value) -> safety::expected<void, std::string>;
[[nodiscard]] auto watch_phys(uint64_t phys_addr) -> safety::expected<void, std::string>;

[[nodiscard]] auto read_phys_32(uint64_t phys_addr) -> safety::expected<uint32_t, std::string>;
[[nodiscard]] auto write_phys_32(uint64_t phys_addr, uint32_t value) -> safety::expected<void, std::string>;
[[nodiscard]] auto watch_phys_32(const WatchOptions& options, const std::atomic<bool>& stop_requested) -> safety::expected<void, std::string>;

} // namespace safety::devmem

#endif // SAFETY_DEVMEM_PHYS_VIEW_HPP
