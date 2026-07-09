// clang-format off
#define TOML_IMPLEMENTATION
#include "kira/Properties.h"
// clang-format on

#include <fmt/format.h>

#include <algorithm>
#include <sstream>
#include <utility>

#include "kira/Assertions.h"

namespace kira {
namespace {
std::optional<std::string>
get_diagnostic_impl(toml::source_region const &region, auto const &sourceLines) {
    // TOML uses 1-based line and column indices.
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

namespace detail {
void erase_used_nodes(
    std::unordered_set<toml::node const *> &usedNodes, toml::node const &node
) noexcept {
    usedNodes.erase(&node);

    if (auto const *table = node.as_table()) {
        for (auto const &[key, child] : *table)
            erase_used_nodes(usedNodes, child);
        return;
    }

    if (auto const *array = node.as_array())
        for (auto const &child : *array)
            erase_used_nodes(usedNodes, child);
}
} // namespace detail

Properties::Properties() : root_{std::make_shared<detail::PropertiesRoot>()} {
    table_ = &root_->table;
}

Properties::Properties(toml::table table, std::string_view source)
    : root_{std::make_shared<detail::PropertiesRoot>()} {
    root_->table = std::move(table);

    std::istringstream stream{std::string{source}};
    std::string line;
    while (std::getline(stream, line))
        root_->sourceLines.push_back(std::move(line));

    table_ = &root_->table;
}

Properties::Properties(toml::table table, SmallVector<std::string> source)
    : root_{std::make_shared<detail::PropertiesRoot>()} {
    root_->table = std::move(table);
    root_->sourceLines = std::move(source);
    table_ = &root_->table;
}

std::optional<std::string> Properties::get_diagnostic_(toml::source_region const &region) const {
    return get_diagnostic_impl(region, root_->sourceLines);
}

PropertiesArray::PropertiesArray() : root_{std::make_shared<detail::PropertiesRoot>()} {
    array_ = &root_->array;
}

PropertiesArray::PropertiesArray(toml::array array)
    : root_{std::make_shared<detail::PropertiesRoot>()} {
    root_->array = std::move(array);
    array_ = &root_->array;
}
} // namespace kira
