#pragma once

// clang-format off
#define TOML_HEADER_ONLY 0
#include <toml++/toml.hpp>
// clang-format on

#include <span>
#include <sstream>

#include "kira/Assertions.h"
#include "kira/SmallVector.h"

namespace kira::v2::detail {
///
class PropertiesImpl {
public:
    virtual ~PropertiesImpl() = default;

    /// Clear the table.
    virtual void clear() = 0;

    /// Create a deep copy of the current \c PropertiesImpl object.
    ///
    /// If the underlying table is a view, a new view is created.
    [[nodiscard]] virtual std::unique_ptr<PropertiesImpl> clone() const = 0;

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

///
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
    PropertiesInstanceImpl(toml::table table, kira::SmallVector<std::string> source)
        : table{std::make_unique<toml::table>(std::move(table))}, sourceLines{std::move(source)} {}

    /// Copy construct from another \c PropertiesInstanceImpl object.
    PropertiesInstanceImpl(PropertiesInstanceImpl const &other)
        : table{std::make_unique<toml::table>(*other.table)}, sourceLines{other.sourceLines} {}

    /// Move construct from another \c PropertiesInstanceImpl object.
    PropertiesInstanceImpl(PropertiesInstanceImpl &&other) noexcept = default;

    /// Copy and swap from another \c PropertiesInstanceImpl object.
    PropertiesInstanceImpl &operator=(PropertiesInstanceImpl other) {
        using std::swap;
        swap(table, other.table);
        swap(sourceLines, other.sourceLines);
        return *this;
    }

public:
    /// \see PropertiesImpl::clear
    void clear() override {
        table->clear();
        sourceLines.clear();
    }

    /// \see PropertiesImpl::clone
    [[nodiscard]] std::unique_ptr<PropertiesImpl> clone() const override {
        return std::make_unique<PropertiesInstanceImpl>(*this);
    }

    /// \see PropertiesImpl::is_view
    [[nodiscard]] bool is_view() const noexcept override { return false; }

    /// \see PropertiesImpl::get_table
    [[nodiscard]] toml::table &get_table() const noexcept override { return *table; }

    /// \see PropertiesImpl::get_source_lines
    [[nodiscard]] std::span<std::string const> get_source_lines() const noexcept override {
        return sourceLines;
    }

private:
    // A pointer is used to store the table, because the original `toml::table` follows the const
    // rule while we don't want to enforce const correctness (or to say, is impossible).
    std::unique_ptr<toml::table> table;
    SmallVector<std::string> sourceLines;
};

///
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

    /// \see PropertiesImpl::clone
    [[nodiscard]] std::unique_ptr<PropertiesImpl> clone() const override {
        return std::make_unique<PropertiesViewImpl>(*this);
    }

    /// \see PropertiesImpl::is_view
    [[nodiscard]] bool is_view() const noexcept override { return true; }

    /// \see PropertiesImpl::get_table
    [[nodiscard]] toml::table &get_table() const noexcept override {
        KIRA_ASSERT(tableView.is_table(), "The view must be a table");
        return *tableView.as_table();
    }

    /// \see PropertiesImpl::get_source_lines
    [[nodiscard]] std::span<std::string const> get_source_lines() const noexcept override {
        return sourceLinesView;
    }

private:
    toml::node_view<toml::node> tableView;
    std::span<std::string const> sourceLinesView;
};

///
class PropertiesArrayImpl {
public:
    virtual ~PropertiesArrayImpl() = default;

    /// Clear the array.
    virtual void clear() = 0;

    /// Create a deep copy of the current \c PropertiesArrayImpl object.
    ///
    /// If the underlying array is a view, a new view is created.
    [[nodiscard]] virtual std::unique_ptr<PropertiesArrayImpl> clone() const = 0;

    /// Indicates whether this \c PropertiesArrayImpl object is a view of
    /// an existing TOML table.
    ///
    /// \return \c true if this object is a view, \c false if it's an independent instance.
    [[nodiscard]] virtual bool is_view() const noexcept = 0;

    /// Provides a direct reference to the underlying TOML array.
    ///
    /// \return A reference to the \c toml::table object.
    ///
    /// \remark This function does not enforce const correctness, as `toml::array` is mutable.
    [[nodiscard]] virtual toml::array &get_array() const noexcept = 0;
};

///
class PropertiesArrayInstanceImpl final : public PropertiesArrayImpl {
public:
    PropertiesArrayInstanceImpl() : array{std::make_unique<toml::array>()} {}

    /// Construct a new array container from a TOML array object.
    explicit PropertiesArrayInstanceImpl(toml::array array)
        : array(std::make_unique<toml::array>(std::move(array))) {}

    /// Copy construct from another \c PropertiesArrayInstanceImpl object.
    PropertiesArrayInstanceImpl(PropertiesArrayInstanceImpl const &other)
        : array{std::make_unique<toml::array>(*other.array)} {}

    ///
    PropertiesArrayInstanceImpl(PropertiesArrayInstanceImpl &&other) noexcept = default;

    /// Copy and swap from another \c PropertiesArrayInstanceImpl object.
    PropertiesArrayInstanceImpl &operator=(PropertiesArrayInstanceImpl other) {
        using std::swap;
        swap(array, other.array);
        return *this;
    }

public:
    /// \see PropertiesArrayImpl::clear
    void clear() override { array->clear(); }

    /// \see PropertiesArrayImpl::clone
    [[nodiscard]] std::unique_ptr<PropertiesArrayImpl> clone() const override {
        return std::make_unique<PropertiesArrayInstanceImpl>(*this);
    }

    /// \see PropertiesArrayImpl::is_view
    [[nodiscard]] bool is_view() const noexcept override { return false; }

    /// \see PropertiesArrayImpl::get_table
    [[nodiscard]] toml::array &get_array() const noexcept override { return *array; }

private:
    // A pointer is used to store the array, because the original `toml::array` follows the const
    // rule while we don't want to enforce const correctness (or to say, is impossible).
    std::unique_ptr<toml::array> array;
};

///
class PropertiesArrayViewImpl final : public PropertiesArrayImpl {
public:
    /// Create a new table view.
    PropertiesArrayViewImpl(toml::node_view<toml::node> arrayView) : arrayView{arrayView} {}

    /// \see PropertiesArrayImpl::clear
    void clear() override {
        KIRA_ASSERT(arrayView.is_array(), "The view must be an array");
        arrayView.as_array()->clear();
    }

    /// \see PropertiesArrayImpl::clone
    [[nodiscard]] std::unique_ptr<PropertiesArrayImpl> clone() const override {
        return std::make_unique<PropertiesArrayViewImpl>(*this);
    }

    /// \see PropertiesArrayImpl::is_view
    [[nodiscard]] bool is_view() const noexcept override { return false; }

    /// \see PropertiesArrayImpl::get_table
    [[nodiscard]] toml::array &get_array() const noexcept override {
        KIRA_ASSERT(arrayView.is_array(), "The view must be an array");
        return *arrayView.as_array();
    }

private:
    toml::node_view<toml::node> arrayView;
};
} // namespace kira::v2::detail
