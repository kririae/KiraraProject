#pragma once

// clang-format off
#define TOML_HEADER_ONLY 0
#include <toml++/toml.hpp>
// clang-format on

#include <magic_enum.hpp>
#include <span>

#include "Assertions.h"
#include "kira/Anyhow.h"

namespace kira::v2 {
namespace detail {
class PropertiesImpl {
public:
    virtual ~PropertiesImpl() = default;

    /// Clear the table.
    virtual void clear() = 0;

    /// Create a deep copy of the current \c PropertiesImpl object.
    ///
    /// If the underlying table is a view, a new view is created.
    virtual std::unique_ptr<PropertiesImpl> clone() const = 0;

    /// Indicates whether this \c PropertiesImpl object is a view of
    /// an existing TOML table.
    ///
    /// \return \c true if this object is a view, \c false if it's an independent instance.
    [[nodiscard]] virtual bool is_view() const noexcept = 0;

    /// Provides a direct reference to the underlying TOML table.
    ///
    /// \return A reference to the \c toml::table object.
    ///
    /// \remark This function does not enforce const correctness, as `toml::table` is mutable.
    [[nodiscard]] virtual toml::table &get_table() const noexcept = 0;

    /// Provides access to the source lines associated with this \c Propperty.
    ///
    /// \return A span of constant strings representing the source lines of the table.
    [[nodiscard]] virtual std::span<std::string const> get_source_lines() const noexcept = 0;
};

class PropertiesInstanceImpl final : public PropertiesImpl {
public:
    /// Create a new table container.
    PropertiesInstanceImpl() : table{std::make_unique<toml::table>()} {}
    /// Create a new table container from a TOML table and the corresponding source file.
    PropertiesInstanceImpl(toml::table table, std::string_view source)
        : table{std::make_unique<toml::table>(std::move(table))} {
        std::istringstream stream{std::string{source}};
        std::string line;
        while (std::getline(stream, line))
            sourceLines.push_back(std::move(line));
    }
    /// Create a new table container from a TOML table and the corresponding source content.
    PropertiesInstanceImpl(toml::table table, SmallVector<std::string> source)
        : table{std::make_unique<toml::table>(std::move(table))}, sourceLines{std::move(source)} {}
    /// Copy construct from another \c PropertiesInstanceImpl object.
    PropertiesInstanceImpl(PropertiesInstanceImpl const &other)
        : table{std::make_unique<toml::table>(*other.table)}, sourceLines{other.sourceLines} {}
    PropertiesInstanceImpl(PropertiesInstanceImpl &&other) noexcept = default;
    /// Copy and swap from another \c PropertiesInstanceImpl object.
    PropertiesInstanceImpl &operator=(PropertiesInstanceImpl other) {
        using std::swap;
        swap(table, other.table);
        swap(sourceLines, other.sourceLines);
        return *this;
    }

public:
    /// \see Properties::clear
    void clear() override {
        table->clear();
        sourceLines.clear();
    }

    /// \see Properties::clone
    [[nodiscard]] std::unique_ptr<PropertiesImpl> clone() const override {
        return std::make_unique<PropertiesInstanceImpl>(*this);
    }

    /// \see Properties::is_view
    [[nodiscard]] bool is_view() const noexcept override { return false; }
    /// \see Properties::get_table
    [[nodiscard]] toml::table &get_table() const noexcept override { return *table; }
    /// \see Properties::get_source_lines
    [[nodiscard]] std::span<std::string const> get_source_lines() const noexcept override {
        return sourceLines;
    }

private:
    std::unique_ptr<toml::table> table;
    SmallVector<std::string> sourceLines;
};

class PropertiesViewImpl final : public PropertiesImpl {
public:
    /// Create a new table view.
    PropertiesViewImpl(
        toml::node_view<toml::node> tableView, std::span<std::string const> sourceLinesView
    )
        : tableView{tableView}, sourceLinesView{sourceLinesView} {}

    void clear() override {
        KIRA_ASSERT(tableView.is_table(), "The view must be a table");
        tableView.as_table()->clear();
        sourceLinesView = {};
    }

    /// \see Properties::clone
    [[nodiscard]] std::unique_ptr<PropertiesImpl> clone() const override {
        return std::make_unique<PropertiesViewImpl>(*this);
    }

    /// \see Properties::is_view
    [[nodiscard]] bool is_view() const noexcept override { return true; }
    /// \see Properties::get_table
    [[nodiscard]] toml::table &get_table() const noexcept override {
        KIRA_ASSERT(tableView.is_table(), "The view must be a table");
        return *tableView.as_table();
    }

    /// \see Properties::get_source_lines
    [[nodiscard]] std::span<std::string const> get_source_lines() const noexcept override {
        return sourceLinesView;
    }

private:
    toml::node_view<toml::node> tableView;
    std::span<std::string const> sourceLinesView;
};
} // namespace detail

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
    Properties get_view(std::string_view const name) const {
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
    Properties get_view() const {
        return {toml::node_view{pImpl->get_table()}, pImpl->get_source_lines()};
    }

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
    [[nodiscard]] std::optional<std::string>
    get_diagnostic_(toml::source_region const &region) const;

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
} // namespace kira::v2
