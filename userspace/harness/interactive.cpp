#include "interactive.hpp"
#include <cstdio>
#include <cstdlib>
#include <format>
#include <fstream>
#include <iostream>

namespace safety {

void PresenterEngine::pause_for_presenter(std::string_view prompt) const {
    std::cout << prompt << std::flush;
    TermiosGuard raw_guard;
    (void)std::getchar();
    std::cout << "\n";
}

void PresenterEngine::notify_monitor_state(std::string_view state, const QuestionSlide& slide) {
    std::ofstream f("/tmp/demo_state", std::ios::trunc);
    if (f.is_open()) {
        f << "STATE=" << state << "\n";
        f << "SCENARIO_ID=" << slide.id << "\n";
        f << "TITLE=" << slide.title << "\n";
        f << "SETUP=" << slide.setup_desc << "\n";
        f << "QUESTION=" << slide.question_text << "\n";
        for (std::size_t i = 0; i < slide.choices.size(); ++i) {
            char key = (i == 0) ? 'A' : (i == 1 ? 'B' : 'C');
            f << "OPT_" << key << "=" << slide.choices[i].key << ") " << slide.choices[i].text << "\n";
        }
        f << "CORRECT=" << slide.correct_option << "\n";
        f << "EXPLANATION=" << slide.explanation << "\n";
        f.flush();
    }
}

void PresenterEngine::ensure_tmux_environment(const std::string& scenario_arg, const std::string& start_at_arg) {
    const char* tmux_env = std::getenv("TMUX");
    if (tmux_env != nullptr && std::string_view(tmux_env).length() > 0) {
        return;
    }

    std::cout << "[+] TMUX environment not detected. Auto-launching demo dashboard session...\n";

    std::string scenario_flag = scenario_arg.empty() ? "" : std::format(" --scenario {}", scenario_arg);
    std::string start_at_flag = start_at_arg.empty() ? "" : std::format(" --start-at {}", start_at_arg);

    std::string tmux_cmd = std::format(
        "tmux new-session -d -s demo -n 'SafetyIsolation' && "
        "tmux split-window -h -t demo:0 && "
        "tmux send-keys -t demo:0.0 'monitor' Enter && "
        "tmux send-keys -t demo:0.1 'harness --interactive{}{}' Enter && "
        "tmux attach-session -t demo:0",
        scenario_flag,
        start_at_flag
    );

    int ret = std::system(tmux_cmd.c_str());
    if (ret == 0) {
        std::exit(0);
    } else {
        std::cerr << "[-] Failed to auto-launch tmux session (exit code " << ret << "). Continuing in single terminal.\n";
    }
}

} // namespace safety
