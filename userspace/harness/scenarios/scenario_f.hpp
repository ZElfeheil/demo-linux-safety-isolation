#ifndef USERSPACE_HARNESS_SCENARIOS_SCENARIO_F_HPP
#define USERSPACE_HARNESS_SCENARIOS_SCENARIO_F_HPP

#include <string>
#include <string_view>
#include "common/expected.hpp"
#include "common/scenario.hpp"
#include "harness/module_loader.hpp"

namespace safety {

/** Demonstrates combined CPU page protection and SMMU isolation. */
class ScenarioF {
public:
    /** Returns the presenter-facing scenario title. */
    [[nodiscard]] static constexpr std::string_view name() noexcept {
        return "Scenario F: Full CTX + SMMU Isolation";
    }

    /**
     * @brief Load the complete protection stack and monitoring modules.
     * @return Success, or the module-loading error.
     */
    auto setup() -> safety::expected<void, std::string>;
    /**
     * @brief Execute the attack and verify that the memory remains protected.
     * @return Result showing whether combined isolation held.
     */
    auto run() -> ScenarioResult;
    /** Removes all modules loaded for this scenario. */
    void teardown();

private:
    ModuleLoader loader_;
};

} // namespace safety

#endif // USERSPACE_HARNESS_SCENARIOS_SCENARIO_F_HPP
