#ifndef USERSPACE_HARNESS_SCENARIOS_SCENARIO_D_HPP
#define USERSPACE_HARNESS_SCENARIOS_SCENARIO_D_HPP

#include <string>
#include <string_view>
#include "common/expected.hpp"
#include "common/scenario.hpp"
#include "harness/module_loader.hpp"

namespace safety {

/** Demonstrates bypass of a protected virtual alias through the linear map. */
class ScenarioD {
public:
    /** Returns the presenter-facing scenario title. */
    [[nodiscard]] static constexpr std::string_view name() noexcept {
        return "Scenario D: DMA Linear Map Bypass";
    }

    /**
     * @brief Load page-protection and attacker modules for the demonstration.
     * @return Success, or the module-loading error.
     */
    auto setup() -> safety::expected<void, std::string>;
    /**
     * @brief Execute the linear-map write and check value divergence.
     * @return Result showing whether the alias bypass was observed.
     */
    auto run() -> ScenarioResult;
    /** Removes all modules loaded for this scenario. */
    void teardown();

private:
    ModuleLoader loader_;
};

} // namespace safety

#endif // USERSPACE_HARNESS_SCENARIOS_SCENARIO_D_HPP
