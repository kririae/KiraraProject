#pragma once

#include <magic_enum.hpp>

#include "kira/Anyhow.h"
#include "kira/detail/PropertiesImpl.h"

namespace kira::v2 {
class PropertiesArray;

/// The to-be-specialized PropertyProcessor class that converts TOML native types <-> C++ types.
template <class> struct PropertyProcessor : std::false_type {};

/// The associative class that stores properties in a TOML table.
///
/// This class doesn't enforce const correctness, as the underlying table can be modified, and one
/// can easily create a mutable view from a const view.
class Properties {
public:
    template <typename T> friend struct PropertyProcessor;

    Properties();

    /// Copy construct from another \c Properties object.
    ///
    /// \remark This is a deep copy operation, i.e., the underlying table and source are copied.
    Properties(Properties const &other) : pImpl{other.pImpl->clone()}, useMap{other.useMap} {}

    /// Move construct from another \c Properties object.
    Properties(Properties &&) noexcept = default;

    /// Copy and swap from another \c Properties object.
    Properties &operator=(Properties other) {
        using std::swap;
        swap(pImpl, other.pImpl);
        swap(useMap, other.useMap);
        return *this;
    }

    /// Construct a new \c Properties object from a TOML table and the corresponding source file.
    Properties(toml::table table, std::string_view source);
    /// copydoc Properties(toml::table, std::string_view)
    Properties(toml::table table, SmallVector<std::string> source);

    /// Construct a new \c PropertiesView object from a TOML table node view and the corresponding
    /// source file.
    ///
    /// \remark tableView must be a valid table node, otherwise segfault will occur.
    Properties(toml::node_view<toml::node> tableView, std::span<std::string const> sourceLinesView);

    /// Clear the table and source.
    ///
    /// \remark This is an unsafe operation, as it will invalidate all the views to the \c
    /// Properties.
    void clear() const noexcept { pImpl->clear(); }

    /// Check if the table is empty.
    [[nodiscard]] bool empty() const noexcept { return pImpl->get_table().empty(); }

    /// Check if the table is a view or instance.
    [[nodiscard]] bool is_view() const noexcept { return pImpl->is_view(); }

    /// Clone a copy of the underlying table.
    ///
    /// This is useful when you want to create a new \c Properties out of a view object.
    [[nodiscard]] Properties clone() const { return {pImpl->get_table(), std::string_view{}}; }

public:
    /// Check if the table contains the given key neglecting the type.
    ///
    /// \param name The name of the key to check.
    /// \return \c true if the key exists, \c false otherwise.
    [[nodiscard]] bool contains(std::string_view const name) const noexcept {
        return get_node_(name) != nullptr;
    }

    /// Check if the table contains the given key that can be converted to the given type.
    ///
    /// This tests the possibility of invoking \c get<T>(name) without throwing an exception.
    ///
    /// \tparam T The type to check for conversion.
    ///
    /// \param name The name of the key to check.
    ///
    /// \return \c true if the key exists and can be converted to type \c T, \c false otherwise.
    template <typename T>
    [[nodiscard]] bool is_type_of(std::string_view const name) const noexcept
        requires(PropertyProcessor<T>::value)
    {
        auto *node = get_node_(name);
        if (!node)
            return false;
        try {
            PropertyProcessor<T>::from_toml(*node, *this);
            return true;
        } catch (...) { return false; }
    }

    /// Generic getter method to retrieve properties.
    ///
    /// \tparam T The type of the property to retrieve. Can additionally be a \c Properties, \c
    /// PropertiesView<true>, or \c PropertiesView<false>.
    ///
    /// \param name The name of the property to retrieve.
    ///
    /// \return The property value of type \c T.
    /// \throw Anyhow if the key does not exist.
    template <typename T>
    T get(std::string_view const name) const
        requires(PropertyProcessor<T>::value)
    {
        auto *node = get_node_(name);
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

    /// Generic getter method to retrieve properties with a default value.
    ///
    /// This function will only return the default value if the key does not exist. Other exceptions
    /// like conversion failure (as defined by the PropertyProcess itself) will be propagated.
    ///
    /// \tparam T The type of the property to retrieve.
    ///
    /// \param name The name of the property to retrieve.
    /// \param defaultValue The default value to return if the key does not exist.
    ///
    /// \return The property value of type \c T, or the default value if the key does not exist.
    template <typename T> T get_or(std::string_view const name, T const &defaultValue) const {
        if (auto const *node = get_node_(name); !node)
            return defaultValue;
        return get<T>(name);
    }

    /// Get a view to the property with the given key.
    ///
    /// \param name The name of the key to get the view of.
    ///
    /// \return A view to the property with the given key.
    /// \throw Anyhow if the key does not exist or the key is not a table.
    [[nodiscard]] Properties get_view(std::string_view const name) const {
        auto *node = get_node_(name);
        if (!node)
            throw Anyhow("Key '{}' does not exist", name);

        if (node->is_table()) {
            auto *table = node->as_table();
            return {toml::node_view{*table}, pImpl->get_source_lines()};
        }

        if (auto const diagnostic = get_diagnostic_(node->source())) {
            throw Anyhow(
                "Expected a table, but got a(an) {}: {}", magic_enum::enum_name(node->type()),
                diagnostic.value()
            );
        }

        throw Anyhow("Expected a table, but got a(an) {}", magic_enum::enum_name(node->type()));
    }

    /// Get a view to this property.
    ///
    /// \return A view to this property.
    [[nodiscard]] Properties get_view() const {
        return {toml::node_view{pImpl->get_table()}, pImpl->get_source_lines()};
    }

    /// Get a view to the array property with the given key.
    [[nodiscard]] inline PropertiesArray get_array_view(std::string_view const name) const;

    /// Generic setter method to set properties.
    ///
    /// \param name The name of the property to set.
    /// \param value The value to set the property to.
    /// \param overwrite If \c true, the property will be overwritten if it already exists. If \c
    /// false and the property already exists, an exception is thrown. Default is \c true.
    template <typename T>
    void set(std::string_view const name, T const &value, bool overwrite = true) {
        if (auto const *node = get_node_(name); node && !overwrite) {
            throw Anyhow(
                "Key '{}' already exists{}", name, get_diagnostic_(node->source()).value_or("")
            );
        }

        mark_unused(std::string{name});
        pImpl->get_table().insert_or_assign(
            name, PropertyProcessor<T>::to_toml(value), toml::preserve_source_value_flags
        );
    }

    /// Get the TOML representation of the table.
    [[nodiscard]] std::string to_toml() const {
        std::ostringstream oss;
        oss << pImpl->get_table();
        return oss.str();
    }

    /// Get the JSON representation of the table.
    [[nodiscard]] std::string to_json() const {
        std::ostringstream oss;
        oss << toml::json_formatter{pImpl->get_table()};
        return oss.str();
    }

    /// Get the YAML representation of the table.
    [[nodiscard]] std::string to_yaml() const {
        std::ostringstream oss;
        oss << toml::yaml_formatter{pImpl->get_table()};
        return oss.str();
    }

public:
    /// Check if the property with the given key is used.
    ///
    /// \param key The key of the property to check.
    /// \return \c true if the property is used, \c false otherwise.
    /// \throw std::exception If the property is not found, an exception is thrown.
    [[nodiscard]] bool is_used(std::string const &key) const { return useMap.at(key); }

    /// Check if all properties are used.
    ///
    /// \return \c true if all properties are used, \c false otherwise.
    [[nodiscard]] bool is_all_used() const {
        return std::ranges::all_of(useMap, [](auto const &pair) { return pair.second; });
    }

    /// Mark the property with the given key as used.
    ///
    /// \param key The key of the property to mark as used.
    /// \remark If the key does not exist, a new entry is created with the value \c true.
    void mark_used(std::string const &key) { useMap[key] = true; }

    /// Mark the property with the given key as unused.
    ///
    /// \param key The key of the property to mark as unused.
    /// \remark If the key does not exist, a new entry is created with the value \c false.
    void mark_unused(std::string const &key) { useMap[key] = false; }

    /// Iterate through all unused properties.
    ///
    /// Example: \code{.cpp}
    /// Properties props;
    /// // ... initialize
    /// props.for_each_unused([](std::string_view key) {
    ///    std::cout << "Unused key: " << key << "\n";
    /// });
    /// \endcode
    void for_each_unused(auto const &func) const {
        for (auto const &[key, used] : useMap)
            if (!used)
                func(key);
    }

private:
    /// Get the underlying node of the given key.
    [[nodiscard]] toml::node *get_node_(std::string_view const key) const noexcept {
        return pImpl->get_table().get(key);
    }

    /// Get the diagnostic message for the given source region.
    [[nodiscard]] std::optional<std::string> get_diagnostic_(toml::source_region const &region
    ) const;

    /// Populate the use map from the table.
    void populate_use_map_() {
        // Insert all keys with \c false value into the \c useMap for a later query.
        for (auto const &[key, value] : pImpl->get_table())
            useMap.emplace(std::string(key), false);
    }

    ///
    std::unique_ptr<detail::PropertiesImpl> pImpl;
    ///
    std::unordered_map<std::string, bool> useMap;
};

/// The array structure that stores TOML nodes.
class PropertiesArray {
public:
    template <typename T> friend struct PropertyProcessor;

    PropertiesArray();

    /// Copy construct from another \c PropertiesArray object.
    ///
    /// \remark This is a deep copy operation, i.e., the underlying array is copied.
    PropertiesArray(PropertiesArray const &other) : pImpl{other.pImpl->clone()} {}

    /// Move construct from another \c PropertiesArray object.
    PropertiesArray(PropertiesArray &&) noexcept = default;

    /// Copy and swap from another \c Properties object.
    PropertiesArray &operator=(PropertiesArray other) {
        using std::swap;
        swap(pImpl, other.pImpl);
        return *this;
    }

    /// Construct a new \c Properties object from a TOML table and the corresponding source file.
    PropertiesArray(toml::array array);

    /// Construct a new \c Properties object from a TOML array node view and the corresponding
    /// source
    PropertiesArray(toml::node_view<toml::node> arrayView);

    /// Clear the array and source.
    ///
    /// \remark This is an unsafe operation, as it will invalidate all the views to the \c
    /// PropertiesArray.
    void clear() const noexcept { pImpl->clear(); }

    /// Check if the array is empty.
    [[nodiscard]] bool empty() const noexcept { return pImpl->get_array().empty(); }

    /// Check if the array is a view or instance.
    [[nodiscard]] bool is_view() const noexcept { return pImpl->is_view(); }

    /// Get the size of the array.
    [[nodiscard]] size_t size() const noexcept { return pImpl->get_array().size(); }

    /// Clone a copy of the underlying array.
    ///
    /// This is useful when you want to create a new \c PropertiesArray out of a view object.
    [[nodiscard]] PropertiesArray clone() const { return {get_array_()}; }

public:
    /// Check if the i-th element is of the given type.
    ///
    /// This tests the possibility of invoking \c get<T>(index) without throwing an exception.
    ///
    /// \tparam T The type to check for conversion.
    ///
    /// \param index The index of the element to check.
    ///
    /// \return \c true if the element exists and can be converted to type \c T, \c false otherwise.
    /// \see Properties::is_type_of
    template <typename T> [[nodiscard]] bool is_type_of(std::size_t index) const noexcept {
        auto *node = get_array_().get(index);
        if (!node)
            return false;
        try {
            PropertyProcessor<T>::from_toml(*node);
            return true;
        } catch (...) { return false; }
    }

    /// Generic getter method to retrieve properties.
    ///
    /// \tparam T The type of the property to retrieve.
    ///
    /// \param index The index of the element to retrieve.
    /// \return The property value of type \c T.
    ///
    /// \throw Anyhow if the index is out of bounds or the element cannot be converted to type \c T.
    template <typename T>
    T get(std::size_t index) const
        requires(PropertyProcessor<T>::value)
    {
        auto *node = get_array_().get(index);
        if (!node) {
            std::ostringstream oss;
            oss << get_array_();
            throw Anyhow("Index '{}' out of bounds in the array: \n{}\n", index, oss.str());
        }

        try {
            return PropertyProcessor<T>::from_toml(*node);
        } catch (std::exception const &e) {
            // TODO(krr): No diagnostic info is available for array elements for now, because of
            // this weird design:
            // https://github.com/marzer/tomlplusplus/issues/49#issuecomment-665089577
            //
            // I don't really understand why the source info is not copied and there is no other
            // approach to copy it without forking the library, do it later.
            std::ostringstream oss;
            oss << get_array_();
            throw Anyhow(
                "Failed to convert element at index {} to type {}: {} in the array: \n{}\n", index,
                PropertyProcessor<T>::name, e.what(), oss.str()
            );
        }
    }

    /// Generic getter method to retrieve properties with a default value.
    ///
    /// \tparam T The type of the property to retrieve.
    ///
    /// \param index The index of the element to retrieve.
    /// \param defaultValue The default value to return if the index is out of bounds.
    ///
    /// \return The property value of type \c T, or the default value if the index is out of bounds.
    template <typename T>
    T get_or(std::size_t index, T const &defaultValue)
        requires(PropertyProcessor<T>::value)
    {
        auto *node = get_array_().get(index);
        if (!node)
            return defaultValue;
        return get<T>(index);
    }

    /// \copydoc get_or(std::size_t, T const &)
    template <typename T>
    T get_or(std::size_t index, T const &defaultValue) const
        requires(PropertyProcessor<T>::value)
    {
        auto *node = get_array_().get(index);
        if (!node)
            return defaultValue;
        return get<T>(index);
    }

    /// Get a view to the property with the given index.
    [[nodiscard]] Properties get_view(std::size_t index) const {
        auto *node = get_array_().get(index);
        if (!node) {
            std::ostringstream oss;
            oss << get_array_();
            throw Anyhow("Index '{}' out of bounds in the array: \n{}\n", index, oss.str());
        }

        if (node->is_table()) {
            auto *table = node->as_table();
            return {toml::node_view{*table}, std::span<std::string const>{}};
        }

        throw Anyhow("Expected an array, but got a(an) {}", magic_enum::enum_name(node->type()));
    }

    /// Get a view to the array property with the given index.
    [[nodiscard]] PropertiesArray get_array_view(std::size_t index) const {
        auto *node = get_array_().get(index);
        if (!node) {
            std::ostringstream oss;
            oss << get_array_();
            throw Anyhow("Index '{}' out of bounds in the array: \n{}\n", index, oss.str());
        }

        if (node->is_array()) {
            auto *array = node->as_array();
            return {toml::node_view{*array}};
        }

        throw Anyhow("Expected an array, but got a(an) {}", magic_enum::enum_name(node->type()));
    }

    /// Get a view to this array.
    [[nodiscard]] PropertiesArray get_array_view() const { return {toml::node_view{get_array_()}}; }

    /// Generic setter method to set properties.
    ///
    /// \tparam T The type of the property to set.
    ///
    /// \param index The index of the element to set.
    /// \param value The value to set the property to.
    ///
    /// \throw std::exception if the index is out of bounds.
    template <typename T>
    void set(std::size_t index, T const &value)
        requires(PropertyProcessor<T>::value)
    {
        if (index >= get_array_().size()) {
            std::ostringstream oss;
            oss << get_array_();
            throw Anyhow("Index '{}' out of bounds in the array: \n{}\n", index, oss.str());
        }

        auto const begin = get_array_().begin();
        get_array_().replace(
            begin + static_cast<ptrdiff_t>(index), PropertyProcessor<T>::to_toml(value),
            toml::preserve_source_value_flags
        );
    }

    /// Push a value to the back of the array.
    ///
    /// \tparam T The type of the property to push.
    ///
    /// \param value The value to push to the back of the array.
    template <typename T>
    void push_back(T const &value)
        requires(PropertyProcessor<T>::value)
    {
        get_array_().push_back(PropertyProcessor<T>::to_toml(value));
    }

private:
    /// Get the underlying array.
    [[nodiscard]] toml::array &get_array_() const noexcept { return pImpl->get_array(); }

    std::unique_ptr<detail::PropertiesArrayImpl> pImpl;
};

// postpone the definition of get_array_view
PropertiesArray Properties::get_array_view(std::string_view const name) const {
    auto *node = get_node_(name);
    if (!node)
        throw Anyhow("Key '{}' does not exist", name);

    if (node->is_array()) {
        auto *array = node->as_array();
        return {toml::node_view{*array}};
    }

    if (auto const diagnostic = get_diagnostic_(node->source())) {
        throw Anyhow(
            "Expected an array, but got a(an) {}: {}", magic_enum::enum_name(node->type()),
            diagnostic.value()
        );
    }

    throw Anyhow("Expected an array, but got a(an) {}", magic_enum::enum_name(node->type()));
}

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
        if (auto const &value = node.template value<std::string>())
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
    static auto to_toml(auto const &props) { return props.pImpl->get_table(); }
    static auto from_toml(auto &node, auto...) {
        if (!node.is_table())
            throw Anyhow("Expected a table, but got a(an) {}", magic_enum::enum_name(node.type()));
        return Properties{*node.as_table(), std::string_view{}};
    }
};

template <> struct PropertyProcessor<PropertiesArray> : std::true_type {
    static constexpr std::string_view name = "kira::PropertiesArray";
    static auto to_toml(auto const &props) { return props.get_array_(); }
    static auto from_toml(auto &node, auto...) {
        if (!node.is_array())
            throw Anyhow("Expected an array, but got a(an) {}", magic_enum::enum_name(node.type()));
        return PropertiesArray{*node.as_array()};
    }
};
} // namespace kira::v2
