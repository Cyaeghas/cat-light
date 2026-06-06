#include "agent.hpp"
#include "agent_scan.hpp"
#include "app.hpp"
#include "hooks.hpp"
#include "render.hpp"
#include "server.hpp"
#include "sync.hpp"

#include <iostream>
#include <sstream>

using namespace catlight;

int main(int argc, char **argv) {
  if (argc >= 2 && to_lower(argv[1]) == "event") {
    try {
      bool use_stdin = false;
      for (int i = 2; i < argc; ++i) {
        if (std::string(argv[i]) == "--stdin") {
          use_stdin = true;
        }
      }
      std::string error;
      std::optional<AgentEvent> event;
      if (use_stdin) {
        std::ostringstream input;
        input << std::cin.rdbuf();
        std::string text = trim(input.str());
        if (!text.empty()) {
          std::string parse_error;
          Json json = Json::parse(text, &parse_error);
          if (!parse_error.empty()) {
            std::cerr << "event JSON parse failed: " << parse_error << "\n";
            return 2;
          }
          event = agent_event_from_json(json, &error);
        }
      }
      if (!event) {
        event = agent_event_from_cli(argc, argv, 2, &error);
      }
      if (!event) {
        std::cerr << error << "\n";
        return 2;
      }
      if (!append_agent_event(*event, &error)) {
        std::cerr << error << "\n";
        return 1;
      }
      std::cout << agent_event_to_json(*event).dump() << "\n";
      return 0;
    } catch (const std::exception &e) {
      std::cerr << "cat-light event: " << e.what() << "\n";
      return 1;
    }
  }

  std::string command;
  std::string error;
  Options options = parse_options(argc, argv, command, error);
  if (!error.empty()) {
    std::cerr << error << "\n\n" << render_help();
    return 2;
  }

  command = to_lower(command);
  if (command == "help" || command == "--help" || command == "-h") {
    std::cout << render_help();
    return 0;
  }

  try {
    if (command == "status") {
      std::cout << render_status_text(collect_snapshot(options)) << "\n";
      return 0;
    }
    if (command == "json") {
      std::cout << snapshot_to_json(collect_snapshot(options)).dump(2) << "\n";
      return 0;
    }
    if (command == "waybar") {
      std::cout << render_waybar_json(collect_snapshot(options)) << "\n";
      return 0;
    }
    if (command == "state") {
      std::cout << render_agent_state_text(collect_agent_sessions(options)) << "\n";
      return 0;
    }
    if (command == "sessions") {
      std::cout << render_agent_sessions_text(collect_agent_sessions(options)) << "\n";
      return 0;
    }
    if (command == "sessions-json") {
      std::cout << agent_sessions_to_json(collect_agent_sessions(options)).dump(2) << "\n";
      return 0;
    }
    if (command == "agent-waybar") {
      std::cout << render_agent_waybar_json(collect_agent_sessions(options)) << "\n";
      return 0;
    }
    if (command == "sync") {
      std::cout << render_sync_text(sync_history(options)) << "\n";
      return 0;
    }
    if (command == "sync-json") {
      std::cout << sync_stats_to_json(sync_history(options)).dump(2) << "\n";
      return 0;
    }
    if (command == "history") {
      std::cout << render_history_text(options) << "\n";
      return 0;
    }
    if (command == "history-json") {
      std::cout << history_to_json(options).dump(2) << "\n";
      return 0;
    }
    if (command == "history-summary") {
      std::cout << render_history_summary_text(summarize_history(options)) << "\n";
      return 0;
    }
    if (command == "history-summary-json") {
      std::cout << history_summary_to_json(summarize_history(options)).dump(2) << "\n";
      return 0;
    }
    if (command == "history-trends-json") {
      std::cout << history_trends_to_json(options).dump(2) << "\n";
      return 0;
    }
    if (command == "hook-script") {
      std::cout << render_hook_script(options, argv[0]);
      return 0;
    }
    if (command == "hook-install") {
      std::cout << install_hook_scripts(options, argv[0]);
      return 0;
    }
    if (command == "hook-uninstall") {
      std::cout << uninstall_hooks(options);
      return 0;
    }
    if (command == "hook-status") {
      std::cout << render_hook_status(options);
      return 0;
    }
    if (command == "doctor") {
      std::cout << render_doctor_text(run_doctor(options));
      return 0;
    }
    if (command == "doctor-json") {
      std::cout << doctor_to_json(run_doctor(options)).dump(2) << "\n";
      return 0;
    }
    if (command == "serve") {
      return run_server(options);
    }

    std::cerr << "unknown command: " << command << "\n\n" << render_help();
    return 2;
  } catch (const std::exception &e) {
    std::cerr << "cat-light: " << e.what() << "\n";
    return 1;
  }
}
