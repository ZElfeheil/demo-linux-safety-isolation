#ifndef USERSPACE_HARNESS_SCENARIOS_SCENARIO_B_HPP
#define USERSPACE_HARNESS_SCENARIOS_SCENARIO_B_HPP

#include <string>
#include <string_view>
#include "common/expected.hpp"
#include "common/scenario.hpp"
#include "harness/module_loader.hpp"

namespace safety {

/** Demonstrates a mutex bypass by an uncooperative kernel thread. */
class ScenarioB {
public:
    /** Returns the presenter-facing scenario title. */
    [[nodiscard]] static constexpr std::string_view name() noexcept {
        return "Scenario B: Mutex + Rogue Thread";
    }

    /**
     * @brief Load the modules required for the mutex demonstration.
     * @return Success, or the module-loading error.
     */
    auto setup() -> safety::expected<void, std::string>;
    /**
     * @brief Trigger the rogue write and evaluate the observed result.
     * @return Result showing whether the mutex was bypassed as expected.
     */
    auto run() -> ScenarioResult;
    /** Removes all modules loaded for this scenario. */
    void teardown();

private:
    ModuleLoader loader_;
};

} // namespace safety

#endif // USERSPACE_HARNESS_SCENARIOS_SCENARIO_B_HPP
