#pragma once

#include <magic_enum.hpp>
#include <span>

#include "kira/SmallVector.h"
#include "kira/detail/PropertiesMixins.h"

namespace kira {
/// The to-be-specialized PropertyProcessor class that converts TOML native types <-> C++ types.
template <class> struct PropertyProcessor : std::false_type {};

// -----------------------------------------------------------------------------------------------------------------
/// Properties
// -----------------------------------------------------------------------------------------------------------------

/// The associative class that stores properties in a TOML table.
class Properties final : public detail::PropertiesAccessMixin<Properties>,
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
    /// \remark This is an unsafe operation, as it will invalidate all the views to the \c
    /// Properties.
    void clear() noexcept {
        table.clear();
        sourceLines.clear();
        useMap.clear();
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

// -----------------------------------------------------------------------------------------------------------------
// PropertiesView
// -----------------------------------------------------------------------------------------------------------------

/// The PropertiesView class that provides a view to the Properties object.
///
/// \tparam is_mutable Whether the view is mutable or not, i.e., can it modify the underlying table.
/// \remark The PropertiesView class tracks only the source info and the underlying table, but not
/// the usage status as tracked by \c PropertiesUseQueryMixin.
template <bool is_mutable>
class PropertiesView final : public detail::PropertiesAccessMixin<PropertiesView<is_mutable>>,
                             public detail::PropertiesUseQueryMixin {
public:
    using ViewType = std::conditional_t<is_mutable, toml::node, toml::node const>;

    template <typename T> friend struct PropertyProcessor;
    friend class detail::PropertiesAccessMixin<PropertiesView>;
    friend class detail::PropertiesUseQueryMixin;

    PropertiesView() = delete;
    PropertiesView(PropertiesView const &) = default;
    PropertiesView(PropertiesView &&) noexcept = default;
    PropertiesView &operator=(PropertiesView const &) = default;
    PropertiesView &operator=(PropertiesView &&) noexcept = default;

    /// Construct a new \c PropertiesView object from a TOML table node view and the corresponding
    /// source file.
    ///
    /// \remark tableView must be a valid table node, otherwise segfault will occur.
    PropertiesView(
        toml::node_view<ViewType> tableView, std::span<std::string const> sourceLinesView
    )
        : detail::PropertiesUseQueryMixin(*tableView.as_table()), tableView(tableView),
          sourceLinesView(sourceLinesView) {}

    /// Clear the table.
    ///
    /// By clearing the table there, we do not mean by reset the reference, but to clear the
    /// underlying content in the referenced table.
    void clear() noexcept
        requires(is_mutable)
    {
        get_table_().clear();
        sourceLinesView = {};
        useMap.clear();
    }

    /// Check if the table is empty.
    [[nodiscard]] bool empty() const noexcept { return tableView.as_table()->empty(); }

    /// Clone a copy of the underlying table.
    [[nodiscard]] auto clone() const {
        // As `toml::table` will not copy the source info, no source info is copied here.
        return Properties{*tableView.as_table(), ""};
    }

private:
    /// Get the underlying table.
    [[nodiscard]] toml::table &get_table_() noexcept
        requires(is_mutable)
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

// -----------------------------------------------------------------------------------------------------------------
// PropertiesArray
// -----------------------------------------------------------------------------------------------------------------

class PropertiesArray final : public detail::PropertiesArrayAccessMixin<PropertiesArray> {
public:
    template <typename T> friend struct PropertyProcessor;
    friend class detail::PropertiesArrayAccessMixin<PropertiesArray>;

    PropertiesArray() = default;
    PropertiesArray(PropertiesArray const &) = default;
    PropertiesArray(PropertiesArray &&) noexcept = default;
    PropertiesArray &operator=(PropertiesArray const &) = default;
    PropertiesArray &operator=(PropertiesArray &&) noexcept = default;

    /// Construct a new \c PropertiesArray object from a TOML array.
    PropertiesArray(toml::array array) : array(std::move(array)) {}

    /// Clear the array.
    ///
    /// \remark This is an unsafe operation, as it will invalidate all the views to the \c
    /// PropertiesArray.
    void clear() noexcept { array.clear(); }

    /// Check if the array is empty.
    [[nodiscard]] bool empty() const noexcept { return array.empty(); }

    /// Get the size of the array.
    [[nodiscard]] auto size() const noexcept { return array.size(); }

private:
    [[nodiscard]] toml::array &get_array_() noexcept { return array; }
    [[nodiscard]] toml::array const &get_array_() const noexcept { return array; }

    toml::array array;
};

// -----------------------------------------------------------------------------------------------------------------
// PropertiesArrayView
// -----------------------------------------------------------------------------------------------------------------

template <bool is_mutable>
class PropertiesArrayView final
    : public detail::PropertiesArrayAccessMixin<PropertiesArrayView<is_mutable>> {
public:
    using ViewType = std::conditional_t<is_mutable, toml::node, toml::node const>;

    template <typename T> friend struct PropertyProcessor;
    friend class detail::PropertiesArrayAccessMixin<PropertiesArrayView>;

    PropertiesArrayView() = delete;
    PropertiesArrayView(PropertiesArrayView const &) = default;
    PropertiesArrayView(PropertiesArrayView &&) noexcept = default;
    PropertiesArrayView &operator=(PropertiesArrayView const &) = default;
    PropertiesArrayView &operator=(PropertiesArrayView &&) noexcept = default;

    /// Construct a new \c PropertiesArrayView object from a TOML array node view.
    ///
    /// \remark arrayView must be a valid array node, otherwise segfault will occur.
    PropertiesArrayView(toml::node_view<ViewType> arrayView) : arrayView(arrayView) {}

    /// Clear the array.
    ///
    /// By clearing the array here, we do not mean by resetting the reference, but by clearing the
    /// content inside the referenced array. The key will be retained.
    void clear() noexcept
        requires(is_mutable)
    {
        return get_array_().clear();
    }

    /// Check if the array is empty.
    [[nodiscard]] bool empty() const noexcept { return get_array_().empty(); }

    /// Get the size of the array.
    [[nodiscard]] auto size() const noexcept { return get_array_().size(); }

    /// Clone a copy of the underlying array.
    [[nodiscard]] auto clone() const { return PropertiesArray{get_array_()}; }

private:
    [[nodiscard]] toml::array &get_array_() noexcept
        requires(is_mutable)
    {
        return *arrayView.as_array();
    }

    [[nodiscard]] toml::array const &get_array_() const noexcept { return *arrayView.as_array(); }

    toml::node_view<ViewType> arrayView;
};

using MutablePropertiesArrayView = PropertiesArrayView<true>;
using ImmutablePropertiesArrayView = PropertiesArrayView<false>;

// -----------------------------------------------------------------------------------------------------------------
// PropertyProcessor
// -----------------------------------------------------------------------------------------------------------------

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

// This gives the possibility to invoke `arr.push_back("str")`.
template <size_t N> struct PropertyProcessor<char[N]> : std::true_type {
    static constexpr std::string_view name = "std::string";
    static auto to_toml(auto const &v) { return std::string{v}; }
    static auto from_toml(auto &&node, auto...) {
        auto const &value = node.template value<std::string>();
        if (value)
            return value.value();
        throw Anyhow("Expected a {}, but got a(an) {}", name, magic_enum::enum_name(node.type()));
    }
};

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

template <bool is_mutable> struct PropertyProcessor<PropertiesView<is_mutable>> : std::true_type {
    using ViewType = toml::node_view<typename PropertiesView<is_mutable>::ViewType>;

    static constexpr std::string_view name = "kira::PropertiesView";
    static auto to_toml(auto const &props) { return props.clone().table; }

    /// Construct a PropertiesView from only a TOML node.
    static auto from_toml(auto &node, auto...) {
        if (!node.is_table())
            throw Anyhow("Expected a table, but got a(an) {}", magic_enum::enum_name(node.type()));
        return PropertiesView<is_mutable>(ViewType{node}, std::span<std::string const>{});
    }

    /// Construct a PropertiesView from a TOML node and a Properties object.
    static auto from_toml(auto &node, Properties &props) {
        if (!node.is_table())
            throw Anyhow("Expected a table, but got a(an) {}", magic_enum::enum_name(node.type()));
        return PropertiesView<is_mutable>(
            ViewType{node}, std::span{props.sourceLines.begin(), props.sourceLines.end()}
        );
    }

    /// Construct a PropertiesView from a TOML node and another PropertiesView object.
    template <bool is_mutable_>
    static auto from_toml(auto &node, PropertiesView<is_mutable_> &props) {
        if constexpr (not is_mutable_)
            static_assert(
                !is_mutable,
                "Cannot construct an immutable PropertiesView from a mutable PropertiesView"
            );
        if (!node.is_table())
            throw Anyhow("Expected a table, but got a(an) {}", magic_enum::enum_name(node.type()));
        return PropertiesView<is_mutable>(ViewType{node}, props.sourceLinesView);
    }
};

template <> struct PropertyProcessor<PropertiesArray> : std::true_type {
    static constexpr std::string_view name = "kira::PropertiesArray";
    static auto to_toml(auto const &props) { return props.array; }
    static auto from_toml(auto &node, auto...) {
        if (!node.is_array())
            throw Anyhow("Expected an array, but got a(an) {}", magic_enum::enum_name(node.type()));
        return PropertiesArray{*node.as_array()};
    }
};

template <bool is_mutable>
struct PropertyProcessor<PropertiesArrayView<is_mutable>> : std::true_type {
    using ViewType = toml::node_view<typename PropertiesArrayView<is_mutable>::ViewType>;

    static constexpr std::string_view name = "kira::PropertiesArrayView";
    static auto to_toml(auto const &props) { return props.clone().array; }
    static auto from_toml(auto &node, auto...) {
        if (!node.is_array())
            throw Anyhow("Expected an array, but got a(an) {}", magic_enum::enum_name(node.type()));
        return PropertiesArrayView<is_mutable>{ViewType{node}};
    }
};
} // namespace kira
