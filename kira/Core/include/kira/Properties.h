#pragma once

// clang-format off
#define TOML_HEADER_ONLY 0
#include <toml++/toml.hpp>
// clang-format on

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <magic_enum/magic_enum.hpp>
#include <memory>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <type_traits>
#include <unordered_set>
#include <utility>

#include "kira/Anyhow.h"
#include "kira/SmallVector.h"

namespace kira {
class Properties;
class PropertiesArray;

/// \brief Converts between TOML nodes and user-facing property values.
template <class> struct PropertyProcessor : std::false_type {};

namespace detail {
struct PropertiesRoot {
    toml::table table;
    toml::array array;
    SmallVector<std::string> sourceLines;
    std::unordered_set<toml::node const *> usedNodes;
};

void erase_used_nodes(
    std::unordered_set<toml::node const *> &usedNodes, toml::node const &node
) noexcept;
} // namespace detail

/// \brief Handle for a TOML table.
///
/// Copies alias the same backing storage. Use \c clone() when a detached table is needed.
/// A handle returned by \c get_view() shares ownership of the root storage.
/// Replacing or clearing a node invalidates handles that point into that node.
class Properties {
public:
    template <typename T> friend struct PropertyProcessor;
    friend class PropertiesArray;

    Properties();
    Properties(Properties const &) = default;
    Properties(Properties &&) noexcept = default;
    Properties &operator=(Properties const &) = default;
    Properties &operator=(Properties &&) noexcept = default;

    /// \brief Constructs a root handle from a TOML table and source text.
    ///
    /// \param table Table moved into the new root.
    /// \param source Source text used for diagnostics.
    Properties(toml::table table, std::string_view source);

    /// \brief Constructs a root handle from a TOML table and pre-split source lines.
    ///
    /// \param table Table moved into the new root.
    /// \param source Source lines used for diagnostics.
    Properties(toml::table table, SmallVector<std::string> source);

    /// \brief Clears the current table.
    void clear() noexcept {
        for (auto const &[key, node] : *table_)
            detail::erase_used_nodes(root_->usedNodes, node);
        table_->clear();
    }

    /// \brief Returns whether the current table has no entries.
    [[nodiscard]] bool empty() const noexcept { return table_->empty(); }

    /// \brief Copies the current table into a detached root.
    ///
    /// Usage marks are not copied.
    [[nodiscard]] Properties clone() const { return {*table_, root_->sourceLines}; }

public:
    /// \brief Returns whether \p name exists, without checking its type.
    ///
    /// \param name Key to query.
    [[nodiscard]] bool contains(std::string_view name) const noexcept {
        return table_->get(name) != nullptr;
    }

    /// \brief Returns whether \p name can be read as \c T.
    ///
    /// \param name Key to query.
    template <typename T>
    [[nodiscard]] bool is_type_of(std::string_view name) const noexcept
        requires(PropertyProcessor<T>::value)
    {
        auto *node = table_->get(name);
        if (!node)
            return false;
        try {
            PropertyProcessor<T>::from_toml(*node, *this);
            return true;
        } catch (...) { return false; }
    }

    /// \brief Reads \p name as \c T without marking the key used.
    ///
    /// \param name Key to read.
    /// \throw Anyhow if the key is missing or conversion fails.
    template <typename T>
    T get(std::string_view name) const
        requires(PropertyProcessor<T>::value)
    {
        auto *node = table_->get(name);
        if (!node)
            throw Anyhow("Key '{}' does not exist", name);

        try {
            return PropertyProcessor<T>::from_toml(*node, *this);
        } catch (std::exception const &e) {
            throw Anyhow(
                "Failed to convert key '{}' to the type {}: {}{}", name, PropertyProcessor<T>::name,
                e.what(), get_diagnostic_(node->source()).value_or("")
            );
        }
    }

    /// \brief Reads \p name as \c T, or returns \p defaultValue when the key is missing.
    ///
    /// Existing keys still go through normal conversion and may throw. This does not mark the key
    /// used.
    template <typename T>
    T get_or(std::string_view name, T const &defaultValue) const
        requires(PropertyProcessor<T>::value)
    {
        if (!table_->get(name))
            return defaultValue;
        return get<T>(name);
    }

    /// \brief Reads \p name as \c T and marks the key used after a successful read.
    template <typename T>
    T use(std::string_view name) const
        requires(PropertyProcessor<T>::value)
    {
        auto value = get<T>(name);
        (void)mark_used(name);
        return value;
    }

    /// \brief Reads \p name as \c T or returns \p defaultValue, marking only existing keys.
    template <typename T>
    T use_or(std::string_view name, T const &defaultValue) const
        requires(PropertyProcessor<T>::value)
    {
        if (!table_->get(name))
            return defaultValue;
        return use<T>(name);
    }

    /// \brief Returns a table handle for \p name without marking the key used.
    [[nodiscard]] Properties get_view(std::string_view name) const {
        auto *node = table_->get(name);
        if (!node)
            throw Anyhow("Key '{}' does not exist", name);

        if (auto *table = node->as_table())
            return {root_, *table};

        if (auto const diagnostic = get_diagnostic_(node->source()))
            throw Anyhow(
                "Expected a table, but got a(an) {}: {}", magic_enum::enum_name(node->type()),
                diagnostic.value()
            );

        throw Anyhow("Expected a table, but got a(an) {}", magic_enum::enum_name(node->type()));
    }

    /// \brief Returns a table handle for this table.
    [[nodiscard]] Properties get_view() const { return {root_, *table_}; }

    /// \brief Returns a table handle for \p name and marks the key used after success.
    [[nodiscard]] Properties use_view(std::string_view name) const {
        auto view = get_view(name);
        (void)mark_used(name);
        return view;
    }

    /// \brief Returns an array handle for \p name without marking the key used.
    [[nodiscard]] PropertiesArray get_array_view(std::string_view name) const;

    /// \brief Returns an array handle for \p name and marks the key used after success.
    [[nodiscard]] PropertiesArray use_array_view(std::string_view name) const;

    /// \brief Sets \p name to \p value.
    ///
    /// \param overwrite When false, an existing key throws instead of being replaced.
    template <typename T>
    void set(std::string_view name, T const &value, bool overwrite = true)
        requires(PropertyProcessor<T>::value)
    {
        if (auto *node = table_->get(name)) {
            if (!overwrite)
                throw Anyhow(
                    "Key '{}' already exists{}", name, get_diagnostic_(node->source()).value_or("")
                );
            detail::erase_used_nodes(root_->usedNodes, *node);
        }

        table_->insert_or_assign(
            name, PropertyProcessor<T>::to_toml(value), toml::preserve_source_value_flags
        );
    }

    /// \brief Serializes the current table as TOML.
    [[nodiscard]] std::string to_toml() const {
        std::ostringstream oss;
        oss << *table_;
        return oss.str();
    }

    /// \brief Serializes the current table as JSON.
    [[nodiscard]] std::string to_json() const {
        std::ostringstream oss;
        oss << toml::json_formatter{*table_};
        return oss.str();
    }

    /// \brief Serializes the current table as YAML.
    [[nodiscard]] std::string to_yaml() const {
        std::ostringstream oss;
        oss << toml::yaml_formatter{*table_};
        return oss.str();
    }

public:
    /// \brief Returns whether \p key has been marked used.
    ///
    /// Missing keys return false.
    [[nodiscard]] bool is_used(std::string_view key) const noexcept {
        if (auto const *node = table_->get(key))
            return root_->usedNodes.contains(node);
        return false;
    }

    /// \brief Returns whether every current key has been marked used.
    [[nodiscard]] bool is_all_used() const noexcept {
        for (auto const &[key, node] : *table_)
            if (!root_->usedNodes.contains(&node))
                return false;
        return true;
    }

    /// \brief Marks an existing key used.
    ///
    /// \return true when \p key exists.
    [[nodiscard]] bool mark_used(std::string_view key) const {
        if (auto const *node = table_->get(key)) {
            root_->usedNodes.insert(node);
            return true;
        }
        return false;
    }

    /// \brief Marks an existing key unused.
    ///
    /// \return true when \p key exists.
    [[nodiscard]] bool mark_unused(std::string_view key) const noexcept {
        if (auto const *node = table_->get(key)) {
            root_->usedNodes.erase(node);
            return true;
        }
        return false;
    }

    /// \brief Calls \p func once for each current key that has not been marked used.
    void for_each_unused(auto const &func) const {
        for (auto const &[key, node] : *table_)
            if (!root_->usedNodes.contains(&node))
                func(std::string_view{key});
    }

private:
    Properties(std::shared_ptr<detail::PropertiesRoot> root, toml::table &table)
        : root_{std::move(root)}, table_{&table} {}

    [[nodiscard]] std::optional<std::string> get_diagnostic_(toml::source_region const &region
    ) const;

    std::shared_ptr<detail::PropertiesRoot> root_;
    toml::table *table_{nullptr};
};

/// \brief Handle for a TOML array.
///
/// Copies alias the same backing storage. Array entries do not have usage marks.
/// Replacing or clearing an element invalidates handles that point into that element.
class PropertiesArray {
public:
    template <typename T> friend struct PropertyProcessor;
    friend class Properties;

    PropertiesArray();
    explicit PropertiesArray(toml::array array);
    PropertiesArray(PropertiesArray const &) = default;
    PropertiesArray(PropertiesArray &&) noexcept = default;
    PropertiesArray &operator=(PropertiesArray const &) = default;
    PropertiesArray &operator=(PropertiesArray &&) noexcept = default;

    /// \brief Clears the current array.
    void clear() noexcept {
        for (auto const &node : *array_)
            detail::erase_used_nodes(root_->usedNodes, node);
        array_->clear();
    }

    /// \brief Returns whether the current array has no entries.
    [[nodiscard]] bool empty() const noexcept { return array_->empty(); }

    /// \brief Returns the array length.
    [[nodiscard]] std::size_t size() const noexcept { return array_->size(); }

    /// \brief Copies the current array into a detached root.
    [[nodiscard]] PropertiesArray clone() const { return PropertiesArray{*array_}; }

public:
    /// \brief Returns whether element \p index can be read as \c T.
    template <typename T>
    [[nodiscard]] bool is_type_of(std::size_t index) const noexcept
        requires(PropertyProcessor<T>::value)
    {
        auto *node = array_->get(index);
        if (!node)
            return false;
        try {
            PropertyProcessor<T>::from_toml(*node, *this);
            return true;
        } catch (...) { return false; }
    }

    /// \brief Reads element \p index as \c T.
    template <typename T>
    T get(std::size_t index) const
        requires(PropertyProcessor<T>::value)
    {
        auto *node = array_->get(index);
        if (!node) {
            std::ostringstream oss;
            oss << *array_;
            throw Anyhow("Index '{}' out of bounds in the array: \n{}\n", index, oss.str());
        }

        try {
            return PropertyProcessor<T>::from_toml(*node, *this);
        } catch (std::exception const &e) {
            std::ostringstream oss;
            oss << *array_;
            throw Anyhow(
                "Failed to convert element at index {} to type {}: {} in the array: \n{}\n", index,
                PropertyProcessor<T>::name, e.what(), oss.str()
            );
        }
    }

    /// \brief Reads element \p index as \c T, or returns \p defaultValue when out of bounds.
    template <typename T>
    T get_or(std::size_t index, T const &defaultValue) const
        requires(PropertyProcessor<T>::value)
    {
        if (!array_->get(index))
            return defaultValue;
        return get<T>(index);
    }

    /// \brief Returns a table handle for element \p index.
    [[nodiscard]] Properties get_view(std::size_t index) const {
        auto *node = array_->get(index);
        if (!node) {
            std::ostringstream oss;
            oss << *array_;
            throw Anyhow("Index '{}' out of bounds in the array: \n{}\n", index, oss.str());
        }

        if (auto *table = node->as_table())
            return {root_, *table};

        throw Anyhow("Expected a table, but got a(an) {}", magic_enum::enum_name(node->type()));
    }

    /// \brief Returns an array handle for element \p index.
    [[nodiscard]] PropertiesArray get_array_view(std::size_t index) const {
        auto *node = array_->get(index);
        if (!node) {
            std::ostringstream oss;
            oss << *array_;
            throw Anyhow("Index '{}' out of bounds in the array: \n{}\n", index, oss.str());
        }

        if (auto *array = node->as_array())
            return {root_, *array};

        throw Anyhow("Expected an array, but got a(an) {}", magic_enum::enum_name(node->type()));
    }

    /// \brief Returns an array handle for this array.
    [[nodiscard]] PropertiesArray get_array_view() const { return {root_, *array_}; }

    /// \brief Replaces element \p index.
    template <typename T>
    void set(std::size_t index, T const &value)
        requires(PropertyProcessor<T>::value)
    {
        auto *node = array_->get(index);
        if (!node) {
            std::ostringstream oss;
            oss << *array_;
            throw Anyhow("Index '{}' out of bounds in the array: \n{}\n", index, oss.str());
        }

        detail::erase_used_nodes(root_->usedNodes, *node);
        array_->replace(
            array_->begin() + static_cast<ptrdiff_t>(index), PropertyProcessor<T>::to_toml(value),
            toml::preserve_source_value_flags
        );
    }

    /// \brief Appends \p value.
    template <typename T>
    void push_back(T const &value)
        requires(PropertyProcessor<T>::value)
    {
        array_->push_back(PropertyProcessor<T>::to_toml(value));
    }

private:
    PropertiesArray(std::shared_ptr<detail::PropertiesRoot> root, toml::array &array)
        : root_{std::move(root)}, array_{&array} {}

    std::shared_ptr<detail::PropertiesRoot> root_;
    toml::array *array_{nullptr};
};

inline PropertiesArray Properties::get_array_view(std::string_view name) const {
    auto *node = table_->get(name);
    if (!node)
        throw Anyhow("Key '{}' does not exist", name);

    if (auto *array = node->as_array())
        return {root_, *array};

    if (auto const diagnostic = get_diagnostic_(node->source()))
        throw Anyhow(
            "Expected an array, but got a(an) {}: {}", magic_enum::enum_name(node->type()),
            diagnostic.value()
        );

    throw Anyhow("Expected an array, but got a(an) {}", magic_enum::enum_name(node->type()));
}

inline PropertiesArray Properties::use_array_view(std::string_view name) const {
    auto view = get_array_view(name);
    (void)mark_used(name);
    return view;
}

#define KIRA_PROPERTY_PROCESSOR(T)                                                                 \
    template <> struct PropertyProcessor<T> : std::true_type {                                     \
        static constexpr std::string_view name = #T;                                               \
        static T to_toml(auto const &v) { return v; }                                              \
        static T from_toml(auto &&node, auto...) {                                                 \
            if (auto const &value = node.template value<T>())                                      \
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
KIRA_PROPERTY_PROCESSOR(float);
KIRA_PROPERTY_PROCESSOR(double);
KIRA_PROPERTY_PROCESSOR(std::string);
#undef KIRA_PROPERTY_PROCESSOR

template <std::size_t N> struct PropertyProcessor<char[N]> : std::true_type {
    static constexpr std::string_view name = "std::string";
    static auto to_toml(auto const &v) { return std::string{v}; }
    static auto from_toml(auto &&node, auto...) {
        if (auto const &value = node.template value<std::string>())
            return value.value();
        throw Anyhow("Expected a {}, but got a(an) {}", name, magic_enum::enum_name(node.type()));
    }
};

template <> struct PropertyProcessor<std::filesystem::path> : std::true_type {
    static constexpr std::string_view name = "std::filesystem::path";
    static auto to_toml(auto const &path) { return path.string(); }
    static auto from_toml(auto &&node, auto...) {
        if (auto const &value = node.template value<std::string>())
            return std::filesystem::path{value.value()};
        throw Anyhow("Expected a {}, but got a(an) {}", name, magic_enum::enum_name(node.type()));
    }
};

template <> struct PropertyProcessor<Properties> : std::true_type {
    static constexpr std::string_view name = "kira::Properties";
    static auto to_toml(Properties const &props) { return *props.table_; }
    static auto from_toml(toml::node &node, auto const &owner) {
        if (auto *table = node.as_table())
            return Properties{owner.root_, *table};
        throw Anyhow("Expected a table, but got a(an) {}", magic_enum::enum_name(node.type()));
    }
};

template <> struct PropertyProcessor<PropertiesArray> : std::true_type {
    static constexpr std::string_view name = "kira::PropertiesArray";
    static auto to_toml(PropertiesArray const &props) { return *props.array_; }
    static auto from_toml(toml::node &node, auto const &owner) {
        if (auto *array = node.as_array())
            return PropertiesArray{owner.root_, *array};
        throw Anyhow("Expected an array, but got a(an) {}", magic_enum::enum_name(node.type()));
    }
};
} // namespace kira
