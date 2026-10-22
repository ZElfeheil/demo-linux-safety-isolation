#include "harness/scenarios/scenario_g.hpp"
#include "common/proc_reader.hpp"
#include "common/demo_constants.hpp"
#include <charconv>
#include <chrono>
#include <optional>
#include <string>
#include <string_view>
#include <thread>

namespace safety {

namespace {

/** Extract an unsigned counter from one `name: value` procfs field. */
auto parse_counter(std::string_view status, std::string_view field) -> std::optional<std::uint64_t> {
    const std::size_t field_pos = status.find(field);
    if (field_pos == std::string_view::npos) {
        return std::nullopt;
    }

    const std::size_t value_begin = status.find_first_not_of(" \t", field_pos + field.size());
    if (value_begin == std::string_view::npos) {
        return std::nullopt;
    }

    const std::size_t value_end = status.find_first_of("\r\n", value_begin);
    const std::string_view value_text = status.substr(value_begin, value_end - value_begin);
    std::uint64_t value = 0;
    const auto [end, error] = std::from_chars(
        value_text.data(), value_text.data() + value_text.size(), value);
    if (error != std::errc{} || end != value_text.data() + value_text.size()) {
        return std::nullopt;
    }
    return value;
}

} // namespace

auto ScenarioG::setup() -> safety::expected<void, std::string> {
    if (auto res = loader_.load("modules/ctx_monitor.ko"); !res) return res;
    if (auto res = loader_.load("modules/safety_mem.ko"); !res) return res;
    if (auto res = loader_.load("modules/mutex_threads.ko"); !res) return res;
    return {};
}

auto ScenarioG::run() -> ScenarioResult {
    const auto start_ts = std::chrono::steady_clock::now();

    // Load rogue_thread with attack_mode=1 (lock metadata attack)
    if (auto res = loader_.load("modules/rogue_thread.ko",
                                std::string(demo::kRogueMetadataModuleParams)); !res) {
        return ScenarioResult{ScenarioStatus::Error, {}, res.error()};
    }

    ProcReader status_reader{std::string(demo::kRogueStatusPath)};
    const auto deadline = start_ts + demo::kMetadataAttackObservationTime;
    std::string last_read_error;

    while (std::chrono::steady_clock::now() < deadline) {
        const auto status = status_reader.read();
        if (status) {
            const auto attack_count = parse_counter(status.value(), demo::kMetadataAttackCountField);
            if (attack_count && *attack_count > 0) {
                const auto end_ts = std::chrono::steady_clock::now();
                const auto latency = static_cast<std::uint64_t>(
                    std::chrono::duration_cast<std::chrono::nanoseconds>(end_ts - start_ts).count());
                return ScenarioResult{
                    ScenarioStatus::Passed,
                    {latency, latency, 0},
                    "Observed " + std::to_string(*attack_count) +
                        " mutex-owner metadata corruption(s) via " +
                        std::string(demo::kRogueStatusPath)};
            }
        } else {
            last_read_error = status.error();
        }

        std::this_thread::sleep_for(demo::kAttackStatusPollInterval);
    }

    const auto end_ts = std::chrono::steady_clock::now();
    const auto latency = static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(end_ts - start_ts).count());
    const std::string detail = last_read_error.empty()
        ? "No mutex metadata corruption was observed before the timeout"
        : "Could not verify mutex metadata corruption: " + last_read_error;
    return ScenarioResult{ScenarioStatus::Failed, {latency, 0, 0}, detail};
}

void ScenarioG::teardown() {
    loader_.unload_all();
}

} // namespace safety
