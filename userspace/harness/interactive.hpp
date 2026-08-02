#ifndef USERSPACE_HARNESS_INTERACTIVE_HPP
#define USERSPACE_HARNESS_INTERACTIVE_HPP

#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <string>
#include <string_view>
#include <vector>
#include <termios.h>
#include <unistd.h>
#include "common/scenario.hpp"
#include "module_loader.hpp"

namespace safety {

class TermiosGuard {
public:
    TermiosGuard() {
        if (::tcgetattr(STDIN_FILENO, &old_termios_) == 0) {
            struct termios raw = old_termios_;
            raw.c_lflag &= ~(ICANON | ECHO);
            if (::tcsetattr(STDIN_FILENO, TCSANOW, &raw) == 0) {
                active_ = true;
            }
        }
    }

    ~TermiosGuard() {
        if (active_) {
            ::tcsetattr(STDIN_FILENO, TCSANOW, &old_termios_);
        }
    }

    TermiosGuard(const TermiosGuard&) = delete;
    TermiosGuard& operator=(const TermiosGuard&) = delete;
    TermiosGuard(TermiosGuard&&) noexcept = default;
    TermiosGuard& operator=(TermiosGuard&&) noexcept = default;

private:
    struct termios old_termios_{};
    bool active_{false};
};

struct Choice {
    char key{'A'};
    std::string text;
    bool is_correct{false};
};

struct QuestionSlide {
    std::string id;
    std::string title;
    std::string setup_desc;
    std::string question_text;
    std::vector<Choice> choices;
    std::string explanation;
    std::string correct_option{"B"};
};

class PresenterEngine {
public:
    explicit PresenterEngine(bool auto_mode = false) : auto_mode_(auto_mode) {}

    template<Scenario S>
    auto run_scenario(S& scenario, const QuestionSlide& slide) -> ScenarioResult;

    static void ensure_tmux_environment(const std::string& scenario_arg = "", const std::string& start_at_arg = "");
    static void pause_for_presenter(std::string_view prompt = "[ Press any key to continue... ]");
    static void notify_monitor_state(std::string_view state, const QuestionSlide& slide);

private:
    bool auto_mode_{false};
};

template<Scenario S>
auto PresenterEngine::run_scenario(S& scenario, const QuestionSlide& slide) -> ScenarioResult {
    ModuleLoader::cleanup_on_signal();
    // Beat 1: SETUP
    std::cout << "\n======================================================================\n";
    std::cout << "  SCENARIO SETUP: " << slide.title << "\n";
    std::cout << "======================================================================\n";
    std::cout << slide.setup_desc << "\n\n";

    notify_monitor_state("SETUP", slide);

    auto setup_res = scenario.setup();
    if (!setup_res) {
        std::cerr << "SETUP FAILED: " << setup_res.error() << "\n";
        return ScenarioResult{ScenarioStatus::Error, {}, setup_res.error()};
    }

    // Beat 2: QUESTION
    std::cout << "----------------------------------------------------------------------\n";
    std::cout << "  ❓ QUESTION: " << slide.question_text << "\n";
    std::cout << "----------------------------------------------------------------------\n";
    for (const auto& choice : slide.choices) {
        std::cout << "    " << choice.key << ") " << choice.text << "\n";
    }
    std::cout << "\n";

    notify_monitor_state("PAUSED", slide);

    if (!auto_mode_) {
        pause_for_presenter("[ AWAITING PRESENTER KEYPRESS ]");
    }

    // Beat 3: REVEAL
    std::cout << "----------------------------------------------------------------------\n";
    std::cout << "  ⚡ REVEAL: Executing Scenario Action...\n";
    std::cout << "----------------------------------------------------------------------\n";

    notify_monitor_state("REVEALED", slide);

    auto result = scenario.run();

    // Beat 4: EXPLAIN
    std::cout << "\n----------------------------------------------------------------------\n";
    std::cout << "  💡 EXPLAIN & RESULT:\n";
    std::cout << "----------------------------------------------------------------------\n";
    for (const auto& choice : slide.choices) {
        if (choice.is_correct) {
            std::cout << "  Correct Answer: " << choice.key << ") " << choice.text << " ✓\n";
        }
    }
    std::cout << "\n" << slide.explanation << "\n";
    std::cout << "======================================================================\n\n";

    scenario.teardown();
    notify_monitor_state("NORMAL", slide);

    return result;
}

} // namespace safety

#endif // USERSPACE_HARNESS_INTERACTIVE_HPP
