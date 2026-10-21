#ifndef USERSPACE_HARNESS_SCENARIOS_SCENARIO_E_HPP
#define USERSPACE_HARNESS_SCENARIOS_SCENARIO_E_HPP

#include <string>
#include <string_view>
#include "common/expected.hpp"
#include "common/scenario.hpp"
#include "harness/module_loader.hpp"

namespace safety {

/** Demonstrates that SMMU protection does not block CPU-originated writes. */
class ScenarioE {
public:
    /** Returns the presenter-facing scenario title. */
    [[nodiscard]] static constexpr std::string_view name() noexcept {
        return "Scenario E: SMMU Active — CPU Write Bypasses SMMU";
    }

    /**
     * @brief Load the SMMU and attacker modules without changing CPU PTEs.
     * @return Success, or the module-loading error.
     */
    auto setup() -> safety::expected<void, std::string>;
    /**
     * @brief Execute the CPU write and evaluate the protected-memory result.
     * @return Result showing the SMMU-versus-CPU distinction.
     */
    auto run() -> ScenarioResult;
    /** Removes all modules loaded for this scenario. */
    void teardown();

private:
    ModuleLoader loader_;
};

} // namespace safety

#endif // USERSPACE_HARNESS_SCENARIOS_SCENARIO_E_HPP
