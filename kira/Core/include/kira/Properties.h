#pragma once

// clang-format off
#define TOML_HEADER_ONLY 0
#include <toml++/toml.hpp>
// clang-format on

#include <magic_enum.hpp>
#include <span>

#include "kira/Anyhow.h"
#include "kira/Assertions.h"
#include "kira/SmallVector.h"
#include "kira/detail/PropertiesMixin.h"

namespace kira {
/// The to-be-specialized PropertyProcessor class that converts TOML native types to C++ types.
template <class T> struct PropertyProcessor : std::false_type {};

namespace detail {
template <class Derived> class PropertiesMixin;
} // namespace detail

class Properties : public detail::PropertiesMixin<Properties> {
    // On standard library like containers, we use the `camel_case` naming convention, as if it is a
    // imported class.

public:
    template <typename T> friend struct PropertyProcessor;
    friend class PropertiesMixin<Properties>;

    Properties() = default;
    Properties(Properties const &) = default;
    Properties(Properties &&) noexcept = default;
    Properties &operator=(Properties const &) = default;
    Properties &operator=(Properties &&) noexcept = default;

    /// Construct a new Properties object from a TOML table and the corresponding source file.
    Properties(toml::table table, std::string_view source);
    /// \copydoc Properties(toml::table, std::string_view)
    Properties(toml::table table, SmallVector<std::string> source);

    /// Clear the table and source.
    void clear() noexcept {
        table.clear();
        sourceLines.clear();
    }

    /// Check if the table is empty.
    [[nodiscard]] bool empty() const noexcept { return table.empty(); }

    /// Get the TOML representation of the table.
    [[nodiscard]] std::string to_toml() const {
        std::ostringstream oss;
        oss << table;
        return oss.str();
    }

private:
    /// Get within the TOML node with the given key.
    [[nodiscard]] auto get_node_(std::string_view key) { return table.get(key); }
    /// \copydoc get_node_(std::string_view)
    [[nodiscard]] auto get_node_(std::string_view key) const { return table.get(key); }

    /// Get the diagnostic message for the given source region.
    [[nodiscard]]
    std::optional<std::string> get_diagnostic_(toml::source_region const &region) const;

private:
    toml::table table;
    SmallVector<std::string> sourceLines;
};

/// The PropertiesView class that provides a view to the Properties object.
///
/// \tparam Mutable Whether the view is mutable or not, i.e., can it modify the underlying table.
template <bool Mutable>
class PropertiesView : public detail::PropertiesMixin<PropertiesView<Mutable>> {
public:
    using ViewType = std::conditional_t<Mutable, toml::node, toml::node const>;

    template <typename T> friend struct PropertyProcessor;
    friend class detail::PropertiesMixin<PropertiesView>;

    PropertiesView() = delete;
    PropertiesView(PropertiesView const &) = default;
    PropertiesView(PropertiesView &&) noexcept = default;
    PropertiesView &operator=(PropertiesView const &) = default;
    PropertiesView &operator=(PropertiesView &&) noexcept = default;

    /// Construct a new PropertiesView object from a TOML table node view and the corresponding
    /// source file.
    PropertiesView(
        toml::node_view<ViewType> tableView, std::span<std::string const> sourceLinesView
    )
        : tableView(tableView), sourceLinesView(sourceLinesView) {
        KIRA_ASSERT(
            tableView.is_table(), "PropertyView should be constructed from a valid table node"
        );
    }

    /// Check if the table is empty.
    [[nodiscard]] bool empty() const noexcept { return tableView.as_table()->empty(); }

    /// Get the TOML representation of the table.
    [[nodiscard]] std::string to_toml() const {
        std::ostringstream oss;
        oss << tableView;
        return oss.str();
    }

private:
    /// Get within the TOML node with the given key.
    [[nodiscard]] auto get_node_(std::string_view key)
        requires(Mutable)
    {
        return tableView.as_table()->get(key);
    }

    /// \copydoc get_node_(std::string_view)
    [[nodiscard]] auto get_node_(std::string_view key) const {
        return tableView.as_table()->get(key);
    }

    /// Get the diagnostic message for the given source region.
    [[nodiscard]]
    std::optional<std::string> get_diagnostic_(toml::source_region const &region) const;

private:
    toml::node_view<ViewType> tableView;
    std::span<std::string const> sourceLinesView;
};

template class PropertiesView<true>;
template class PropertiesView<false>;

using MutablePropertiesView = PropertiesView<true>;
using ImmutablePropertiesView = PropertiesView<false>;

#define KIRA_PROPERTY_PROCESSOR(T)                                                                 \
    template <> struct PropertyProcessor<T> : std::true_type {                                     \
        static constexpr std::string_view name = #T;                                               \
        static T to_toml(const auto &v) { return v; }                                              \
        static T from_toml(auto &&node, auto...) {                                                 \
            auto const &value = node.template value<T>();                                          \
            if (value)                                                                             \
                return value.value();                                                              \
            throw Anyhow(                                                                          \
                false, "Expected a {}, but got a(an) {}", name, magic_enum::enum_name(node.type()) \
            );                                                                                     \
        }                                                                                          \
    }; /**/

KIRA_PROPERTY_PROCESSOR(bool);
KIRA_PROPERTY_PROCESSOR(int);
KIRA_PROPERTY_PROCESSOR(int64_t);
KIRA_PROPERTY_PROCESSOR(uint32_t);
KIRA_PROPERTY_PROCESSOR(uint64_t);
KIRA_PROPERTY_PROCESSOR(float);
KIRA_PROPERTY_PROCESSOR(double);
KIRA_PROPERTY_PROCESSOR(std::string);
#undef KIRA_PROPERTY_PROCESSOR

template <> struct PropertyProcessor<std::filesystem::path> : std::true_type {
    static constexpr std::string_view name = "std::filesystem::path";
    static auto to_toml(auto const &props) { return props.to_toml(); }
    static auto from_toml(toml::node &node, auto...) {
        return std::filesystem::path(node.template value<std::string>().value());
    }
};

template <> struct PropertyProcessor<Properties> : std::true_type {
    static constexpr std::string_view name = "kira::Properties";
    static auto to_toml(auto const &props) { return props.to_toml(); }
    static auto from_toml(auto &node, auto...) {
        if (!node.is_table())
            throw Anyhow(
                false, "Expected a table, but got a(an) {}", magic_enum::enum_name(node.type())
            );
        return Properties{*node.as_table(), std::string_view{}};
    }
};

template <bool Mutable> struct PropertyProcessor<PropertiesView<Mutable>> : std::true_type {
    using ViewType = toml::node_view<typename PropertiesView<Mutable>::ViewType>;

public:
    static constexpr std::string_view name = "kira::PropertiesView";
    static auto to_toml(auto const &props) { return props.to_toml(); }

    /// Construct a PropertiesView from a TOML node and a Properties object.
    static auto from_toml(auto &node, Properties const &props) {
        if (!node.is_table())
            throw Anyhow(
                false, "Expected a table, but got a(an) {}", magic_enum::enum_name(node.type())
            );
        return PropertiesView<Mutable>(
            ViewType{node}, std::span{props.sourceLines.begin(), props.sourceLines.end()}
        );
    }

    /// Construct a PropertiesView from a TOML node and another PropertiesView object.
    template <bool Mutable_>
    static auto from_toml(auto &node, PropertiesView<Mutable_> const &props) {
        static_assert(
            not(Mutable || Mutable_),
            "Cannot construct a mutable PropertiesView from an immutable PropertiesView"
        );
        if (!node.is_table())
            throw Anyhow(
                false, "Expected a table, but got a(an) {}", magic_enum::enum_name(node.type())
            );
        return PropertiesView<Mutable>(ViewType{node}, props.sourceLinesView);
    }
};
} // namespace kira
