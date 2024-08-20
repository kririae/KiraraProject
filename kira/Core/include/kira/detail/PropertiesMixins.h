#pragma once

#include <string_view>
#include <toml++/toml.hpp>

#include "kira/Anyhow.h"

namespace kira {
template <class T> struct PropertyProcessor;

namespace detail {
template <typename Derived> class PropertiesAccessMixin {
public:
    /// Check if the table contains the given key neglecting the type.
    ///
    /// \param name The name of the key to check.
    /// \return \c true if the key exists, \c false otherwise.
    [[nodiscard]] bool contains(std::string_view name) const noexcept {
        return get_node_(name) != nullptr;
    }

    /// Check if the table contains the given key that can be converted to the given type.
    ///
    /// This tests the possibility of invoking \c get<T>(name) without throwing an exception.
    ///
    /// \tparam T The type to check for conversion.
    /// \param name The name of the key to check.
    /// \return \c true if the key exists and can be converted to type \c T, \c false otherwise.
    template <typename T>
    [[nodiscard]] bool is_type_of(std::string_view name) noexcept
        requires(PropertyProcessor<T>::value)
    {
        return type_of_impl_<Derived, T>(derived_(), name);
    }

    /// \copydoc is_type_of(std::string_view)
    template <typename T>
    [[nodiscard]] bool is_type_of(std::string_view name) const noexcept
        requires(PropertyProcessor<T>::value)
    {
        return type_of_impl_<Derived const, T>(derived_(), name);
    }

    /// Generic getter method to retrieve properties.
    ///
    /// \tparam T The type of the property to retrieve. Can additionally be a \c Properties, \c
    /// PropertiesView<true>, or \c PropertiesView<false>.
    /// \param name The name of the property to
    /// retrieve.
    /// \return The property value of type \c T.
    /// \throw Anyhow if the key does not exist.
    template <typename T>
    T get(std::string_view name)
        requires(PropertyProcessor<T>::value)
    {
        return get_node_impl_<Derived, T>(derived_(), name);
    }

    /// \copydoc get(std::string_view)
    template <typename T>
    T get(std::string_view name) const
        requires(PropertyProcessor<T>::value)
    {
        return get_node_impl_<Derived const, T>(derived_(), name);
    }

    /// Generic getter method to retrieve properties with a default value.
    ///
    /// This function will only return the default value if the key does not exist. Other exceptions
    /// like conversion failure (as defined by the PropertyProcess itself) will be propagated.
    ///
    /// \tparam T The type of the property to retrieve.
    /// \param name The name of the property to retrieve.
    /// \param def_value The default value to return if the key does not exist.
    /// \return The property value of type \c T, or the default value if the key does not exist.
    template <typename T> T get_or(std::string_view name, T const &def_value) {
        auto *node = get_node_(name);
        if (!node)
            return def_value;
        return get<T>(name);
    }

    /// \copydoc get_or(std::string_view, T const &)
    template <typename T> T get_or(std::string_view name, T const &def_value) const {
        auto *node = get_node_(name);
        if (!node)
            return def_value;
        return get<T>(name);
    }

    /// Generic setter method to set properties.
    ///
    /// \param name The name of the property to set.
    /// \param value The value to set the property to.
    /// \param overwrite If \c true, the property will be overwritten if it already exists. If \c
    /// false and the property already exists, an exception is thrown. Default is \c true.
    template <typename T> void set(std::string_view name, T const &value, bool overwrite = true) {
        auto *node = get_node_(name);
        if (node && !overwrite) {
            throw Anyhow(
                "Key '{}' already exists: {}", name,
                derived_().get_diagnostic_(node->source()).value_or("")
            );
        }

        derived_().mark_unused(std::string{name});
        derived_().get_table_().insert_or_assign(name, PropertyProcessor<T>::to_toml(value));
    }

    /// Get the TOML representation of the table.
    [[nodiscard]] std::string to_toml() const {
        std::ostringstream oss;
        oss << derived_().get_table_();
        return oss.str();
    }

    /// Get the JSON representation of the table.
    [[nodiscard]] std::string to_json() const {
        std::ostringstream oss;
        oss << toml::json_formatter{derived_().get_table_()};
        return oss.str();
    }

    /// Get the YAML representation of the table.
    [[nodiscard]] std::string to_yaml() const {
        std::ostringstream oss;
        oss << toml::yaml_formatter{derived_().get_table_()};
        return oss.str();
    }

private:
    Derived &derived_() noexcept{ return static_cast<Derived &>(*this); }
    Derived const &derived_() const noexcept { return static_cast<Derived const &>(*this); }

    auto get_node_(std::string_view key) noexcept { return derived_().get_table_().get(key); }
    auto get_node_(std::string_view key) const noexcept { return derived_().get_table_().get(key); }

    // no cxx 23 yet, manually implement the deducing this.
    template <typename Self, typename T>
    static auto type_of_impl_(Self &self, std::string_view name) {
        auto *node = self.get_node_(name);
        if (!node)
            return false;
        try {
            PropertyProcessor<T>::from_toml(*node, self.derived_());
            return true;
        } catch (...) { return false; }
    }

    template <typename Self, typename T>
    static auto get_node_impl_(Self &self, std::string_view name) {
        auto *node = self.get_node_(name);
        if (!node)
            throw Anyhow("Key '{}' does not exist", name);

        try {
            return PropertyProcessor<T>::from_toml(*node, self.derived_());
        } catch (std::exception const &e) {
            throw Anyhow(
                "Failed to convert key '{}' to the type {}: {}{}", name, PropertyProcessor<T>::name,
                e.what(), self.derived_().get_diagnostic_(node->source()).value_or("")
            );
        }
    }
};

/// \brief Mixin class for properties that can be marked as used.
///
/// This mixin class keep tracks of only flat keys and their usage status, i.e., recursive keys are
/// not tracked. Similarly, \c PropertiesView classes hold their own usage status instead of
/// modifying the source's one.
class PropertiesUseQueryMixin {
public:
    /// Default constructor.
    PropertiesUseQueryMixin() = default;

    /// Construct the mixin with the given table.
    ///
    /// \param table The TOML table to initialize the mixin with.
    PropertiesUseQueryMixin(toml::table const &table) {
        // Insert all keys with \c false value into the \c useMap for later query.
        for (auto const &[key, value] : table)
            useMap.emplace(std::string(key), false);
    }

    /// Check if the property with the given key is used.
    ///
    /// \param key The key of the property to check.
    /// \return \c true if the property is used, \c false otherwise.
    /// \throw std::exception If the property is not found, an exception is thrown.
    [[nodiscard]]
    bool is_used(std::string const &key) const {
        return useMap.at(key);
    }

    /// Check if all properties are used.
    ///
    /// \return \c true if all properties are used, \c false otherwise.
    [[nodiscard]]
    bool is_all_used() const {
        return std::all_of(useMap.begin(), useMap.end(), [](auto const &pair) {
            return pair.second;
        });
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
    /// \example
    /// ```cpp
    /// Properties props;
    /// // ... initialize props
    /// props.for_each_unused([](std::string_view key) {
    ///    std::cout << "Unused key: " << key << '\n';
    /// });
    /// ```
    void for_each_unused(auto const &func) const {
        for (auto const &[key, used] : useMap)
            if (!used)
                func(key);
    }

private:
    std::unordered_map<std::string, bool> useMap;
};
} // namespace detail
} // namespace kira
