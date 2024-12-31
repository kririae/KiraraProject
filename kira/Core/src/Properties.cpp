// clang-format off
#define TOML_IMPLEMENTATION
#include "kira/Properties.h"
#include "kira/Properties2.h"
// clang-format on

#include <fmt/color.h>

#include <ostream>

#include "kira/Assertions.h"

namespace kira {
Properties::Properties(toml::table table, std::string_view source)
    : detail::PropertiesUseQueryMixin(table), table{std::move(table)} {
    std::istringstream stream{std::string{source}};
    std::string line;
    while (std::getline(stream, line))
        sourceLines.push_back(std::move(line));
}

Properties::Properties(toml::table table, SmallVector<std::string> source)
    : PropertiesUseQueryMixin(table), table{std::move(table)}, sourceLines{std::move(source)} {}

namespace {
#if 0
auto get_range_decorator(size_t len) {
    return fmt::format(
        "{}",
        fmt::styled(std::string(len, '^'), fmt::fg(fmt::color::light_green) | fmt::emphasis::bold)
    );
}
#endif

std::optional<std::string>
get_diagnostic_impl(toml::source_region const &region, auto const &sourceLines) {
    // invalid region
    if (region.begin.line == 0 || region.begin.column == 0 || region.end.line == 0 ||
        region.end.column == 0)
        return std::nullopt;
    if (region.begin.line - 1 >= sourceLines.size() || region.end.line - 1 >= sourceLines.size())
        return std::nullopt;

    KIRA_ASSERT(region.begin.line > 0, "Start line number must be greater than 0");
    KIRA_ASSERT(region.begin.column > 0, "Start column number must be greater than 0");
    KIRA_ASSERT(region.end.line > 0, "End line number must be greater than 0");
    KIRA_ASSERT(region.end.column > 0, "End column number must be greater than 0");

    std::ostringstream oss;
    auto const maxLineNumber = std::max(region.begin.line, region.end.line);
    auto const lineNumberWidth = std::to_string(maxLineNumber).length() + 2;

    oss << "\n";
    for (auto lineNumber = region.begin.line; lineNumber <= region.end.line; ++lineNumber) {
        auto const &line = sourceLines[lineNumber - 1];
        oss << fmt::format("{0:{1}d} | {2}\n", lineNumber, lineNumberWidth, line);

        if (lineNumber == region.begin.line) {
            oss << fmt::format(
                "{0:{1}} | {2}", "", lineNumberWidth, std::string(region.begin.column - 1, ' ')
            );
            if (lineNumber == region.end.line)
                oss << std::string(region.end.column - region.begin.column, '^');
            else
                oss << std::string(line.length() - region.begin.column + 1, '^');
            oss << '\n';
        } else if (lineNumber == region.end.line) {
            oss << fmt::format(
                "{0:{1}} | {2}^\n", "", lineNumberWidth, std::string(region.end.column - 1, '^')
            );
        } else {
            oss << fmt::format(
                "{0:{1}} | {2}\n", "", lineNumberWidth, std::string(line.length(), '^')
            );
        }
    }

    return oss.str();
}
} // namespace

std::optional<std::string> Properties::get_diagnostic_(toml::source_region const &region) const {
    return get_diagnostic_impl(region, sourceLines);
}

template <>
std::optional<std::string>
PropertiesView<true>::get_diagnostic_(toml::source_region const &region) const {
    return get_diagnostic_impl(region, sourceLinesView);
}

template <>
std::optional<std::string>
PropertiesView<false>::get_diagnostic_(toml::source_region const &region) const {
    return get_diagnostic_impl(region, sourceLinesView);
}
} // namespace kira

namespace kira::v2 {
Properties::Properties() : pImpl{std::make_unique<detail::PropertiesInstanceImpl>()} {
    populate_use_map_();
}

Properties::Properties(toml::table table, std::string_view source)
    : pImpl{std::make_unique<detail::PropertiesInstanceImpl>(std::move(table), source)} {
    populate_use_map_();
}

Properties::Properties(toml::table table, SmallVector<std::string> source)
    : pImpl{std::make_unique<detail::PropertiesInstanceImpl>(std::move(table), std::move(source))
      } {
    populate_use_map_();
}

Properties::Properties(
    toml::node_view<toml::node> tableView, std::span<std::string const> sourceLinesView
)
    : pImpl{std::make_unique<detail::PropertiesViewImpl>(
          std::move(tableView), std::move(sourceLinesView)
      )} {
    populate_use_map_();
}

std::optional<std::string> Properties::get_diagnostic_(toml::source_region const &region) const {
    return get_diagnostic_impl(region, pImpl->get_source_lines());
}
} // namespace kira::v2
