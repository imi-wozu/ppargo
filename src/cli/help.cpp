#include "cli/help.hpp"

#include <format>
#include <string>
#include <string_view>

#include "cli/catalog.hpp"

namespace cli {

auto help_text(HelpTopic topic) -> std::string_view {
    if (topic == HelpTopic::Root) {
        static const std::string root_text = [] {
            std::string rendered =
                "C++'s build system and package manager\n\n"
                "Usage: argo [OPTIONS] [COMMAND]\n\n"
                "Options:\n"
                "  -V, --version                  Print version info and exit\n"
                "  -q, --quiet                    Do not print cargo log "
                "messages\n"
                "  -h, --help                     Print help\n\n"
                "Commands:\n";

            for (const auto& command : catalog::metadata()) {
                rendered += std::format("  {:<15} {}\n", command.root_label,
                                        command.summary);
            }
            return rendered;
        }();
        return root_text;
    }
    return catalog::help_text(topic);
}

auto help_topic_from_name(std::string_view name) -> std::optional<HelpTopic> {
    return catalog::help_topic_from_name(name);
}

}  // namespace cli
