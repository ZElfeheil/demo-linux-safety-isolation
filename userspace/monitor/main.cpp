#include "monitor/renderer.hpp"
#include "common/proc_reader.hpp"
#include <array>
#include <atomic>
#include <cerrno>
#include <charconv>
#include <chrono>
#include <csignal>
#include <cstring>
#include <fcntl.h>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <string_view>
#include <thread>
#include <utility>

using namespace std::chrono_literals;

namespace {

constexpr int kHexRadix = 16;
constexpr std::size_t kHexPrefixLength = 2;
constexpr std::size_t kBeforePrefixLength = 7;
constexpr std::size_t kAfterPrefixLength = 6;
constexpr std::size_t kTraceMessageLimit = 60;
constexpr std::size_t kTraceReadBufferCapacity = 4096;
constexpr std::size_t kRenderBufferCapacity = 8192;
constexpr auto kRefreshInterval = 100ms;
constexpr std::string_view kTraceTimestamp = "live";
constexpr std::string_view kPrimaryTracePipe = "/sys/kernel/tracing/trace_pipe";
constexpr std::string_view kFallbackTracePipe = "/sys/kernel/debug/tracing/trace_pipe";

// NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables)
std::atomic<bool> g_sigwinch_received{false};
// NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables)
std::atomic<bool> g_running{true};

void signal_handler(int sig) {
    if (sig == SIGWINCH) {
        g_sigwinch_received.store(true);
    } else if (sig == SIGINT || sig == SIGTERM) {
        g_running.store(false);
    }
}

std::string trim(std::string_view s) {
    auto start = s.find_first_not_of(" \t\r\n");
    if (start == std::string_view::npos) return "";
    auto end = s.find_last_not_of(" \t\r\n");
    return std::string(s.substr(start, end - start + 1));
}

uint64_t parse_hex64(std::string_view s) {
    std::string_view clean = s;
    if (clean.starts_with("0x") || clean.starts_with("0X")) clean.remove_prefix(kHexPrefixLength);
    uint64_t val = 0;
    auto [ptr, ec] = std::from_chars(clean.data(), clean.data() + clean.size(), val, kHexRadix);
    return (ec == std::errc{}) ? val : 0;
}

uint32_t parse_hex32(std::string_view s) {
    std::string_view clean = s;
    if (clean.starts_with("0x") || clean.starts_with("0X")) clean.remove_prefix(kHexPrefixLength);
    uint32_t val = 0;
    auto [ptr, ec] = std::from_chars(clean.data(), clean.data() + clean.size(), val, kHexRadix);
    return (ec == std::errc{}) ? val : 0;
}

void reset_snapshot(safety::monitor::ComparisonSnapshot& snapshot) {
    snapshot.val_vmalloc.store(0);
    snapshot.val_phys.store(0);
    snapshot.mapping_entry.store(0);
    snapshot.mapping_writable.store(false);
    snapshot.ctx_protected.store(false);
    snapshot.smmu_active.store(false);
    std::lock_guard lock(snapshot.mx);
    snapshot.mapping_level = "--";
    snapshot.status = "--";
}

void update_snapshot(safety::monitor::ComparisonSnapshot& snapshot, std::string_view key, std::string_view value) {
    if (key == "value_via_vmalloc" || key == "val_vmalloc") {
        snapshot.val_vmalloc.store(parse_hex32(value));
    } else if (key == "value_via_phys" || key == "val_phys") {
        snapshot.val_phys.store(parse_hex32(value));
    } else if (key == "mapping_entry_current" || key == "mapping_entry_vmalloc") {
        snapshot.mapping_entry.store(parse_hex64(value));
    } else if (key == "mapping_writable_current" || key == "mapping_writable_vmalloc") {
        snapshot.mapping_writable.store(value == "1");
    } else if (key == "ctx_protected") {
        snapshot.ctx_protected.store(value == "1" || value == "true" || value == "ON");
    } else if (key == "smmu_active") {
        snapshot.smmu_active.store(value == "1" || value == "true" || value == "ON");
    } else if (key == "mapping_level_current" || key == "mapping_level_vmalloc" || key == "status") {
        std::lock_guard lock(snapshot.mx);
        if (key == "mapping_level_current" || key == "mapping_level_vmalloc") {
            snapshot.mapping_level = value;
        } else {
            snapshot.status = value;
        }
    }
}

void sync_scenario_state(safety::monitor::DisplayState& display_state) {
    std::ifstream state_file("/tmp/demo_state");
    if (!state_file.is_open()) {
        return;
    }

    std::string line;
    std::string sc_id;
    std::string sc_title;
    std::string phase;
    while (std::getline(state_file, line)) {
        const auto eq_pos = line.find('=');
        if (eq_pos == std::string::npos) {
            continue;
        }
        const std::string key = trim(line.substr(0, eq_pos));
        const std::string value = trim(line.substr(eq_pos + 1));

        if (key == "SCENARIO_ID") sc_id = value;
        else if (key == "TITLE") sc_title = value;
        else if (key == "PHASE") phase = value;
        else if (key.starts_with("BEFORE_")) update_snapshot(display_state.before, key.substr(kBeforePrefixLength), value);
        else if (key.starts_with("AFTER_")) update_snapshot(display_state.after, key.substr(kAfterPrefixLength), value);
    }

    if (phase == "SETUP") {
        reset_snapshot(display_state.before);
        reset_snapshot(display_state.after);
    }
    if (!sc_id.empty()) {
        std::lock_guard lock(display_state.scenario_mx);
        display_state.scenario_info.id = sc_id;
        if (!sc_title.empty()) display_state.scenario_info.title = sc_title;
    }
}

void append_event(safety::monitor::DisplayState& display_state,
                  std::string source,
                  std::string message,
                  bool is_warning = false,
                  bool is_error = false) {
    safety::monitor::EventEntry event{
        .timestamp = std::string(kTraceTimestamp),
        .source = std::move(source),
        .message = std::move(message),
        .is_warning = is_warning,
        .is_error = is_error
    };

    std::lock_guard lock(display_state.events_mx);
    display_state.events.push_back(std::move(event));
    if (display_state.events.size() > safety::monitor::DisplayState::max_events) {
        display_state.events.erase(display_state.events.begin());
    }
}

int open_trace_pipe(std::string& selected_path) {
    for (const std::string_view path : {kPrimaryTracePipe, kFallbackTracePipe}) {
        const int fd = ::open(std::string(path).c_str(), O_RDONLY | O_NONBLOCK | O_CLOEXEC);
        if (fd >= 0) {
            selected_path = path;
            return fd;
        }
    }
    return -1;
}

std::string compact_trace_message(std::string_view trace_line) {
    constexpr std::array<std::string_view, 3> kDemoEventPrefixes{
        "mutex_threads:",
        "rogue_thread:",
        "VIOLATION:"
    };

    for (const std::string_view prefix : kDemoEventPrefixes) {
        const std::size_t prefix_pos = trace_line.find(prefix);
        if (prefix_pos != std::string_view::npos) {
            return std::string(trace_line.substr(prefix_pos));
        }
    }

    return std::string(trace_line);
}

} // namespace

int main() {
    std::signal(SIGWINCH, signal_handler);
    std::signal(SIGINT, signal_handler);
    std::signal(SIGTERM, signal_handler);

    safety::monitor::DisplayState display_state;
    safety::monitor::TerminalGuard term_guard;
    safety::monitor::Renderer renderer(display_state);

    // 1. Mem Poller Loop (100ms)
    std::jthread mem_poller([&display_state](const std::stop_token& stoken) {
        safety::ProcReader proc_reader("/proc/safety_mem_status");
        while (!stoken.stop_requested() && g_running.load()) {
            auto res = proc_reader.read();
            if (res.has_value()) {
                std::istringstream stream(res.value());
                std::string line;
                while (std::getline(stream, line)) {
                    auto colon_pos = line.find(':');
                    if (colon_pos == std::string::npos) continue;

                    std::string key = trim(line.substr(0, colon_pos));
                    std::string val = trim(line.substr(colon_pos + 1));

                    if (key == "virt_addr") {
                        display_state.virt_addr.store(parse_hex64(val));
                    } else if (key == "phys_addr") {
                        display_state.phys_addr.store(parse_hex64(val));
                    } else if (key == "mapping_entry_before") {
                        display_state.mapping_entry_before.store(parse_hex64(val));
                    } else if (key == "mapping_entry_current") {
                        display_state.mapping_entry_current.store(parse_hex64(val));
                    } else if (key == "mapping_writable_before") {
                        display_state.mapping_writable_before.store(val == "1");
                    } else if (key == "mapping_writable_current") {
                        display_state.mapping_writable_current.store(val == "1");
                    } else if (key == "mapping_level_before") {
                        std::lock_guard lock(display_state.status_mx);
                        display_state.mapping_level_before = val;
                    } else if (key == "mapping_level_current") {
                        std::lock_guard lock(display_state.status_mx);
                        display_state.mapping_level_current = val;
                    } else if (key == "value_via_vmalloc" || key == "val_vmalloc") {
                        display_state.val_vmalloc.store(parse_hex32(val));
                    } else if (key == "value_via_phys" || key == "val_phys") {
                        display_state.val_phys.store(parse_hex32(val));
                    } else if (key == "ctx_protected") {
                        display_state.ctx_protected.store(val == "1" || val == "true" || val == "ON");
                    } else if (key == "smmu_active") {
                        display_state.smmu_active.store(val == "1" || val == "true" || val == "ON");
                    } else if (key == "mutex_owner") {
                        std::lock_guard lock(display_state.status_mx);
                        display_state.mutex_owner = val;
                    } else if (key == "status") {
                        std::lock_guard lock(display_state.status_mx);
                        display_state.status_str = val;
                    }
                }
            } else {
                std::lock_guard lock(display_state.status_mx);
                display_state.status_str = "MODULE_NOT_LOADED";
            }

            sync_scenario_state(display_state);

            std::this_thread::sleep_for(kRefreshInterval);
        }
    });

    // 2. Non-blocking ftrace event streamer (100ms)
    std::jthread event_streamer([&display_state](const std::stop_token& stoken) {
        std::string trace_path;
        const int trace_fd = open_trace_pipe(trace_path);
        if (trace_fd < 0) {
            append_event(display_state, "monitor",
                         std::string("trace_pipe unavailable: ") + std::strerror(errno), true);
            return;
        }

        append_event(display_state, "monitor", "connected to " + trace_path);
        std::array<char, kTraceReadBufferCapacity> read_buffer{};
        std::string pending;

        while (!stoken.stop_requested() && g_running.load()) {
            const ssize_t byte_count = ::read(trace_fd, read_buffer.data(), read_buffer.size());
            if (byte_count > 0) {
                pending.append(read_buffer.data(), static_cast<std::size_t>(byte_count));

                std::size_t newline_pos = 0;
                while ((newline_pos = pending.find('\n')) != std::string::npos) {
                    std::string trace_line = trim(pending.substr(0, newline_pos));
                    pending.erase(0, newline_pos + 1);
                    if (!trace_line.empty()) {
                        const bool warning = trace_line.find("WARN") != std::string::npos;
                        const bool error = trace_line.find("FAIL") != std::string::npos ||
                                           trace_line.find("VIOLATION") != std::string::npos;
                        std::string message = compact_trace_message(trace_line);
                        append_event(display_state, "trace",
                                     message.substr(0, kTraceMessageLimit), warning, error);
                    }
                }
            } else if (byte_count < 0 && errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR) {
                append_event(display_state, "monitor",
                             std::string("trace_pipe read failed: ") + std::strerror(errno), true);
                break;
            }

            std::this_thread::sleep_for(kRefreshInterval);
        }

        ::close(trace_fd);
    });

    // 3. Renderer Loop (100ms refresh cycle)
    std::string draw_buf;
    draw_buf.reserve(kRenderBufferCapacity);

    while (g_running.load()) {
        auto term_size = safety::monitor::TerminalGuard::get_size();
        if (g_sigwinch_received.exchange(false)) {
            // Signal cleared, size updated
        }

        renderer.render(draw_buf, term_size);
        std::cout.write(draw_buf.data(), static_cast<std::streamsize>(draw_buf.size()));
        std::cout.flush();

        std::this_thread::sleep_for(kRefreshInterval);
    }

    return 0;
}
