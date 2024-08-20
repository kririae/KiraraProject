#pragma once

// clang-format off
#define TOML_HEADER_ONLY 0
#include <toml++/toml.hpp>
// clang-format on

#include <magic_enum.hpp>
#include <span>

#include "kira/Anyhow.h"
#include "kira/SmallVector.h"
#include "kira/detail/PropertiesMixins.h"

namespace kira {
/// The to-be-specialized PropertyProcessor class that converts TOML native types to C++ types.
template <class T> struct PropertyProcessor : std::false_type {};

/// The associative class that stores properties in a TOML table.
class Properties : public detail::PropertiesAccessMixin<Properties>,
                   public detail::PropertiesUseQueryMixin {
    // On standard library like containers, we use the `camel_case` naming convention, as if it is
    // an imported class.

public:
    template <typename T> friend struct PropertyProcessor;
    friend class PropertiesAccessMixin<Properties>;

    Properties() = default;
    Properties(Properties const &) = default;
    Properties(Properties &&) noexcept = default;
    Properties &operator=(Properties const &) = default;
    Properties &operator=(Properties &&) noexcept = default;

    /// Construct a new \c Properties object from a TOML table and the corresponding source file.
    Properties(toml::table table, std::string_view source);
    /// \copydoc Properties(toml::table, std::string_view)
    Properties(toml::table table, SmallVector<std::string> source);

    /// Clear the table and source.
    ///
    /// \remark This is a unsafe operation, as it will invalidate all the views to the \c
    /// Properties.
    void clear() noexcept {
        table.clear();
        sourceLines.clear();
    }

    /// Check if the table is empty.
    [[nodiscard]] bool empty() const noexcept { return table.empty(); }

public:
    using ReflectionType = std::string;
    Properties(ReflectionType const &archive) : Properties(toml::parse(archive), archive) {}
    [[nodiscard]] ReflectionType reflection() const { return to_toml(); }

private:
    /// Get the underlying table.
    [[nodiscard]] toml::table &get_table_() noexcept { return table; }
    /// Get the underlying table.
    [[nodiscard]] toml::table const &get_table_() const noexcept { return table; }

    /// Get the diagnostic message for the given source region.
    [[nodiscard]] std::optional<std::string> get_diagnostic_(toml::source_region const &region
    ) const;

private:
    toml::table table;
    SmallVector<std::string> sourceLines;
};

/// The PropertiesView class that provides a view to the Properties object.
///
/// \tparam IsMutable Whether the view is mutable or not, i.e., can it modify the underlying table.
/// \remark The PropertiesView class tracks only the source info and the underlying table, but not
/// the usage status as tracked by \c PropertiesUseQueryMixin.
template <bool IsMutable>
class PropertiesView : public detail::PropertiesAccessMixin<PropertiesView<IsMutable>>,
                       public detail::PropertiesUseQueryMixin {
public:
    using ViewType = std::conditional_t<IsMutable, toml::node, toml::node const>;

    template <typename T> friend struct PropertyProcessor;
    friend class detail::PropertiesAccessMixin<PropertiesView>;
    friend class detail::PropertiesUseQueryMixin;

    PropertiesView() = delete;
    PropertiesView(PropertiesView const &) = default;
    PropertiesView(PropertiesView &&) noexcept = default;
    PropertiesView &operator=(PropertiesView const &) = default;
    PropertiesView &operator=(PropertiesView &&) noexcept = default;

    /// Construct a new PropertiesView object from a TOML table node view and the corresponding
    /// source file.
    ///
    /// \remark tableView must be a valid table node, otherwise segfault will occur.
    PropertiesView(
        toml::node_view<ViewType> tableView, std::span<std::string const> sourceLinesView
    )
        : detail::PropertiesUseQueryMixin(*tableView.as_table()), tableView(tableView),
          sourceLinesView(sourceLinesView) {}

    /// Check if the table is empty.
    [[nodiscard]] bool empty() const noexcept { return tableView.as_table()->empty(); }

    /// Get the TOML representation of the table.
    [[nodiscard]] std::string to_toml() const {
        std::ostringstream oss;
        oss << tableView;
        return oss.str();
    }

    /// Clone a copy of the underlying table.
    [[nodiscard]]
    auto clone() const {
        // As `toml::table` will not copy the source info, no source info is copied here.
        return Properties{*tableView.as_table(), ""};
    }

private:
    /// Get the underlying table.
    [[nodiscard]] toml::table &get_table_() noexcept
        requires(IsMutable)
    {
        return *tableView.as_table();
    }

    /// Get the underlying table.
    [[nodiscard]] toml::table const &get_table_() const noexcept { return *tableView.as_table(); }

    /// Get the diagnostic message for the given source region.
    [[nodiscard]] std::optional<std::string> get_diagnostic_(toml::source_region const &region
    ) const;

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
                "Expected a {}, but got a(an) {}", name, magic_enum::enum_name(node.type())        \
            );                                                                                     \
        }                                                                                          \
    };

KIRA_PROPERTY_PROCESSOR(bool);
KIRA_PROPERTY_PROCESSOR(int);
KIRA_PROPERTY_PROCESSOR(int64_t);
KIRA_PROPERTY_PROCESSOR(uint32_t);
/* KIRA_PROPERTY_PROCESSOR(uint64_t); */ // not supported by toml++
KIRA_PROPERTY_PROCESSOR(float);
KIRA_PROPERTY_PROCESSOR(double);
KIRA_PROPERTY_PROCESSOR(std::string);
#undef KIRA_PROPERTY_PROCESSOR

template <> struct PropertyProcessor<std::filesystem::path> : std::true_type {
    static constexpr std::string_view name = "std::filesystem::path";
    static auto to_toml(auto const &props) { return props.string(); }
    static auto from_toml(toml::node &node, auto...) {
        return std::filesystem::path(node.template value<std::string>().value());
    }
};

template <> struct PropertyProcessor<Properties> : std::true_type {
    static constexpr std::string_view name = "kira::Properties";
    static auto to_toml(auto const &props) { return props.table; }
    static auto from_toml(auto &node, auto...) {
        if (!node.is_table())
            throw Anyhow("Expected a table, but got a(an) {}", magic_enum::enum_name(node.type()));
        return Properties{*node.as_table(), std::string_view{}};
    }
};

template <bool IsMutable> struct PropertyProcessor<PropertiesView<IsMutable>> : std::true_type {
    using ViewType = toml::node_view<typename PropertiesView<IsMutable>::ViewType>;

    static constexpr std::string_view name = "kira::PropertiesView";
    static auto to_toml(auto const &props) { return props.clone().table; }

    /// Construct a PropertiesView from a TOML node and a Properties object.
    static auto from_toml(auto &node, Properties &props) {
        if (!node.is_table())
            throw Anyhow("Expected a table, but got a(an) {}", magic_enum::enum_name(node.type()));
        return PropertiesView<IsMutable>(
            ViewType{node}, std::span{props.sourceLines.begin(), props.sourceLines.end()}
        );
    }

    /// Construct a PropertiesView from a TOML node and another PropertiesView object.
    template <bool IsMutable_>
    static auto from_toml(auto &node, PropertiesView<IsMutable_> &props) {
        if constexpr (not IsMutable_)
            static_assert(
                not IsMutable,
                "Cannot construct an immutable PropertiesView from a mutable PropertiesView"
            );
        if (!node.is_table())
            throw Anyhow("Expected a table, but got a(an) {}", magic_enum::enum_name(node.type()));
        return PropertiesView<IsMutable>(ViewType{node}, props.sourceLinesView);
    }
};
} // namespace kira
