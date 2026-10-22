#ifndef USERSPACE_HARNESS_SCENARIOS_SCENARIO_G_HPP
#define USERSPACE_HARNESS_SCENARIOS_SCENARIO_G_HPP

#include <string>
#include <string_view>
#include "common/expected.hpp"
#include "common/scenario.hpp"
#include "harness/module_loader.hpp"

namespace safety {

/** Demonstrates corruption of mutex metadata stored in writable memory. */
class ScenarioG {
public:
    /** Returns the presenter-facing scenario title. */
    [[nodiscard]] static constexpr std::string_view name() noexcept {
        return "Scenario G: Mutex Metadata Attack";
    }

    /**
     * @brief Load the modules required for the mutex metadata attack.
     * @return Success, or the module-loading error.
     */
    auto setup() -> safety::expected<void, std::string>;
    /**
     * @brief Execute the metadata overwrite and record the outcome.
     * @return Result describing the effect of corrupting lock metadata.
     */
    auto run() -> ScenarioResult;
    /** Removes all modules loaded for this scenario. */
    void teardown();

private:
    ModuleLoader loader_;
};

} // namespace safety

#endif // USERSPACE_HARNESS_SCENARIOS_SCENARIO_G_HPP
