#include "renderer.hpp"
#include "common/proc_reader.hpp"
#include <atomic>
#include <charconv>
#include <chrono>
#include <csignal>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <string_view>
#include <thread>

using namespace std::chrono_literals;

namespace {

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
    if (clean.starts_with("0x") || clean.starts_with("0X")) clean.remove_prefix(2);
    uint64_t val = 0;
    auto [ptr, ec] = std::from_chars(clean.data(), clean.data() + clean.size(), val, 16);
    return (ec == std::errc{}) ? val : 0;
}

uint32_t parse_hex32(std::string_view s) {
    std::string_view clean = s;
    if (clean.starts_with("0x") || clean.starts_with("0X")) clean.remove_prefix(2);
    uint32_t val = 0;
    auto [ptr, ec] = std::from_chars(clean.data(), clean.data() + clean.size(), val, 16);
    return (ec == std::errc{}) ? val : 0;
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
    std::jthread mem_poller([&display_state](std::stop_token stoken) {
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

            std::this_thread::sleep_for(100ms);
        }
    });

    // 2. Event Streamer & State Sync Loop (100ms)
    std::jthread event_streamer([&display_state](std::stop_token stoken) {
        while (!stoken.stop_requested() && g_running.load()) {
            // Read IPC state control file /tmp/demo_state
            std::ifstream state_file("/tmp/demo_state");
            if (state_file.is_open()) {
                std::string line;
                std::string state_str;
                std::string sc_id, sc_title, sc_setup, sc_q, sc_ans, sc_exp;
                std::vector<std::string> sc_opts;

                while (std::getline(state_file, line)) {
                    auto eq_pos = line.find('=');
                    if (eq_pos == std::string::npos) continue;
                    std::string k = trim(line.substr(0, eq_pos));
                    std::string v = trim(line.substr(eq_pos + 1));

                    if (k == "STATE") state_str = v;
                    else if (k == "SCENARIO_ID") sc_id = v;
                    else if (k == "TITLE") sc_title = v;
                    else if (k == "SETUP") sc_setup = v;
                    else if (k == "QUESTION") sc_q = v;
                    else if (k == "OPT_A" || k == "OPT_B" || k == "OPT_C") sc_opts.push_back(v);
                    else if (k == "CORRECT") sc_ans = v;
                    else if (k == "EXPLANATION") sc_exp = v;
                }

                if (state_str == "PAUSED") {
                    display_state.mode.store(safety::monitor::DashboardMode::Paused);
                } else if (state_str == "REVEALED") {
                    display_state.mode.store(safety::monitor::DashboardMode::Revealed);
                } else {
                    display_state.mode.store(safety::monitor::DashboardMode::Normal);
                }

                if (!sc_id.empty()) {
                    std::lock_guard lock(display_state.scenario_mx);
                    display_state.scenario_info.id = sc_id;
                    if (!sc_title.empty()) display_state.scenario_info.title = sc_title;
                    if (!sc_setup.empty()) display_state.scenario_info.setup_text = sc_setup;
                    if (!sc_q.empty()) display_state.scenario_info.question_text = sc_q;
                    if (!sc_opts.empty()) display_state.scenario_info.options = sc_opts;
                    if (!sc_ans.empty()) display_state.scenario_info.correct_option = sc_ans;
                    if (!sc_exp.empty()) display_state.scenario_info.explanation = sc_exp;
                }
            }

            // Also check trace pipe for events
            std::ifstream trace_file("/sys/kernel/tracing/trace_pipe");
            if (!trace_file.is_open()) {
                trace_file.open("/sys/kernel/debug/tracing/trace_pipe");
            }
            if (trace_file.is_open()) {
                std::string tline;
                int read_count = 0;
                while (read_count < 5 && std::getline(trace_file, tline)) {
                    if (tline.empty()) continue;
                    safety::monitor::EventEntry ev{
                        .timestamp = "+0.100s",
                        .source = "kernel_trace",
                        .message = tline.substr(0, std::min<std::size_t>(tline.size(), 60)),
                        .is_warning = (tline.find("WARN") != std::string::npos),
                        .is_error = (tline.find("FAIL") != std::string::npos || tline.find("VIOLATION") != std::string::npos)
                    };
                    std::lock_guard lock(display_state.events_mx);
                    display_state.events.push_back(ev);
                    if (display_state.events.size() > display_state.max_events) {
                        display_state.events.erase(display_state.events.begin());
                    }
                    read_count++;
                }
            }

            std::this_thread::sleep_for(100ms);
        }
    });

    // 3. Renderer Loop (100ms refresh cycle)
    std::string draw_buf;
    draw_buf.reserve(8192);

    while (g_running.load()) {
        auto term_size = safety::monitor::TerminalGuard::get_size();
        if (g_sigwinch_received.exchange(false)) {
            // Signal cleared, size updated
        }

        renderer.render(draw_buf, term_size);
        std::cout.write(draw_buf.data(), static_cast<std::streamsize>(draw_buf.size()));
        std::cout.flush();

        std::this_thread::sleep_for(100ms);
    }

    return 0;
}
