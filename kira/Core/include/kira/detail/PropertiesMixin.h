#pragma once

#include <string_view>

#include "kira/Anyhow.h"

namespace kira {
template <class T> struct PropertyProcessor;

namespace detail {
template <typename Derived> class PropertiesMixin {
public:
    // -----------------------------------------------------------------------------------------------------------------
    /// \name Element access and lookup
    // -----------------------------------------------------------------------------------------------------------------
    /// \{

    /// Check if the table contains the given key neglecting the type.
    [[nodiscard]] bool contains(std::string_view name) const noexcept {
        return derived_().get_node_(name) != nullptr;
    }

    /// Check if the table contains the given key that can be converted to the given type.
    ///
    /// This tests the possibility of invoking `get<T>(name)` without throwing an exception.
    template <typename T>
    [[nodiscard]] bool type_of(std::string_view name) noexcept
        requires(PropertyProcessor<T>::value)
    {
        return type_of_impl_<Derived, T>(derived_(), name);
    }

    /// \copydoc type_of(std::string_view)
    template <typename T>
    [[nodiscard]] bool type_of(std::string_view name) const noexcept
        requires(PropertyProcessor<T>::value)
    {
        return type_of_impl_<Derived const, T>(derived_(), name);
    }

    /// Generic getter method to retrieve properties.
    ///
    /// \throw Anyhow if the key does not exist.
    /// \remark T can additionally be a `Properties`, `PropertiesView<true>`,
    /// `PropertiesView<false>`.
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

    /// Generic getter method to retrieve properties. (use default value only if no such a entry
    /// exists)
    ///
    /// \note This function will only return the default value if the key does not exist, other
    /// exceptions like conversion failure (as defined by the PropertyProcess itself) will be
    /// propagated.
    template <typename T> T get_or(std::string_view name, T const &def_value) {
        auto *node = derived_().get_node_(name);
        if (!node)
            return def_value;
        return get<T>(name);
    }

    /// \copydoc get_or(std::string_view, T const &)
    template <typename T> T get_or(std::string_view name, T const &def_value) const {
        auto *node = derived_().get_node_(name);
        if (!node)
            return def_value;
        return get<T>(name);
    }

    // -----------------------------------------------------------------------------------------------------------------
    /// \}
    // -----------------------------------------------------------------------------------------------------------------

private:
    Derived &derived_() { return static_cast<Derived &>(*this); }
    Derived const &derived_() const { return static_cast<Derived const &>(*this); }

    // no cxx 23 yet, manually implement the deducing this.
    template <typename Self, typename T>
    static auto type_of_impl_(Self &self, std::string_view name) {
        auto *node = self.derived_().get_node_(name);
        if (!node)
            return false;
        try {
            PropertyProcessor<T>::from_toml(*node, self.derived_());
            return true;
        } catch (...) { return false; }
    }

    template <typename Self, typename T>
    static auto get_node_impl_(Self &self, std::string_view name) {
        auto *node = self.derived_().get_node_(name);
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
} // namespace detail
} // namespace kira
