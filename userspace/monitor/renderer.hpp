#ifndef USERSPACE_MONITOR_RENDERER_HPP
#define USERSPACE_MONITOR_RENDERER_HPP

#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <vector>
#include <sys/ioctl.h>
#include <unistd.h>
#include "common/expected.hpp"
#include "common/demo_constants.hpp"

namespace safety::monitor {

inline constexpr uint16_t kDefaultTerminalCols = 80;
inline constexpr uint16_t kDefaultTerminalRows = 24;

struct ScenarioInfo {
    std::string id{"None"};
    std::string title{"Waiting for a scenario"};
};

/** One immutable monitor snapshot captured on either side of a scenario action. */
struct ComparisonSnapshot {
    std::atomic<uint32_t> val_vmalloc{0};
    std::atomic<uint32_t> val_phys{0};
    std::atomic<uint64_t> mapping_entry{0};
    std::atomic<bool> mapping_writable{false};
    std::atomic<bool> ctx_protected{false};
    std::atomic<bool> smmu_active{false};
    mutable std::mutex mx{};
    std::string mapping_level{"--"};
    std::string status{"--"};
};

struct EventEntry {
    std::string timestamp{};
    std::string source{};
    std::string message{};
    bool is_warning{false};
    bool is_error{false};
};

struct DisplayState {
    std::atomic<uint32_t> val_vmalloc{demo::kSafetySentinel};
    std::atomic<uint32_t> val_phys{demo::kSafetySentinel};
    std::atomic<uint64_t> virt_addr{0};
    std::atomic<uint64_t> phys_addr{0};
    std::atomic<uint64_t> mapping_entry_before{0};
    std::atomic<uint64_t> mapping_entry_current{0};
    std::atomic<bool>     ctx_protected{false};
    std::atomic<bool>     smmu_active{false};
    std::atomic<bool>     mapping_writable_before{false};
    std::atomic<bool>     mapping_writable_current{false};

    ComparisonSnapshot before{};
    ComparisonSnapshot after{};
    mutable std::mutex status_mx{};
    std::string mutex_owner{"none"};
    std::string status_str{"PROTECTED_RO"};
    std::string mapping_level_before{"unknown"};
    std::string mapping_level_current{"unknown"};

    mutable std::mutex events_mx{};
    std::vector<EventEntry> events{};
    inline static constexpr std::size_t max_events{100};

    mutable std::mutex scenario_mx{};
    ScenarioInfo scenario_info{};
};

struct TerminalSize {
    uint16_t cols{kDefaultTerminalCols};
    uint16_t rows{kDefaultTerminalRows};
};

class TerminalGuard {
public:
    TerminalGuard();
    ~TerminalGuard();

    TerminalGuard(const TerminalGuard&) = delete;
    TerminalGuard& operator=(const TerminalGuard&) = delete;
    TerminalGuard(TerminalGuard&&) noexcept = default;
    TerminalGuard& operator=(TerminalGuard&&) noexcept = default;

    [[nodiscard]] static auto get_size() noexcept -> TerminalSize;
};

class Renderer {
public:
    /** Bind the renderer to the shared monitor state. */
    explicit Renderer(const DisplayState& state) : state_(&state) {}

    /** Render the current state into a terminal-sized output buffer. */
    void render(std::string& out_buf, TerminalSize size) const;

private:
    /** Render the normal dashboard state. */
    void render_normal(std::string& out, TerminalSize size) const;
    const DisplayState* state_{nullptr};
};

} // namespace safety::monitor

#endif // USERSPACE_MONITOR_RENDERER_HPP
