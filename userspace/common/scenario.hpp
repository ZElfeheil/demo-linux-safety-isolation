#ifndef USERSPACE_COMMON_SCENARIO_HPP
#define USERSPACE_COMMON_SCENARIO_HPP

#include <concepts>
#include <cstdint>
#include <string>
#include <string_view>
#include "expected.hpp"

namespace safety {

enum class ScenarioStatus : std::uint8_t {
    Passed,
    Failed,
    Skipped,
    Error
};

struct ScenarioLatencyMetrics {
    uint64_t latency_ns{0};
    uint64_t detection_latency_ns{0};
    uint64_t tlb_flush_latency_ns{0};
};

struct ScenarioResult {
    ScenarioStatus status{ScenarioStatus::Passed};
    ScenarioLatencyMetrics metrics{};
    std::string error_message{};
};

// C++ Core Guidelines T.10: concepts for template constraints
template<typename T>
concept Scenario = requires(T s) {
    { s.name() } -> std::convertible_to<std::string_view>;
    { s.setup() } -> std::same_as<safety::expected<void, std::string>>;
    { s.run() } -> std::same_as<ScenarioResult>;
    { s.teardown() } -> std::same_as<void>;
};

} // namespace safety

#endif // USERSPACE_COMMON_SCENARIO_HPP
