#pragma once
/// \file NodeMixins.h
/// \brief Defines mixin classes for extending scene graph node functionality.
///
/// This file provides `NodeMixin` for adding visitor pattern support and
/// `SerializableMixin` for adding serialization capabilities to node classes.

#include <cereal/archives/binary.hpp>
#include <type_traits>

#include "Core/Serialization.h"
#include "Visitors.h"

#if defined(__GNUC__)
#include <cxxabi.h>
#endif

namespace krd {
#if defined(__GNUC__)
namespace details {
// from https://stackoverflow.com/questions/12877521/human-readable-type-info-name
/// \brief Demangles a C++ type name.
/// \param mangled The mangled type name.
/// \return The demangled, human-readable type name, or an error message.
inline std::string demangle(char const *mangled) {
    int status;
    std::unique_ptr<char[], void (*)(void *)> result(
        abi::__cxa_demangle(mangled, 0, 0, &status), std::free
    );
    return result.get() ? std::string(result.get()) : "error occurred";
}
} // namespace details
#endif

/// \brief A mixin class that extends a base node class with visitor pattern functionality.
///
/// `NodeMixin` implements the visitor pattern for derived scene graph nodes.
/// It provides type-safe visitor dispatching by downcasting to the derived type (`Derived`)
/// when `accept` is called. It also provides a default implementation for `clone`
/// and `getTypeName`.
///
/// \tparam Derived The concrete node class that inherits from this mixin.
/// \tparam Base The base class (usually `krd::Node` or a class derived from it)
///              that `Derived` would normally inherit from.
template <class Derived, class Base> class NodeMixin : public Base {
public:
    static_assert(
        std::is_base_of_v<Node, Base>, "NodeMixin's Base must be krd::Node or derive from it."
    );

    /// Type alias for the parent class
    using Parent = Base;

    /// \brief Accepts a visitor and dispatches to the appropriate `apply` method on the visitor.
    ///
    /// This method implements the double-dispatch mechanism of the visitor pattern.
    /// It downcasts the current object (`this`) to its concrete type (`Derived`)
    /// and then calls `visitor.apply()` with the downcasted object.
    ///
    /// \param visitor The visitor object that will process this node.
    void accept(Visitor &visitor) override {
        // Generate the derived class visitor application
        visitor.apply(static_cast<Derived &>(*this));
    }

    /// \brief Accepts a const visitor and dispatches to the appropriate `apply` method on the
    /// visitor.
    ///
    /// This is the const-qualified version of the `accept` method, intended for use
    /// with const visitors that do not modify the visited node.
    ///
    /// \param visitor The const visitor object that will process this node.
    void accept(ConstVisitor &visitor) const override {
        /// Generate the const derived class visitor application
        visitor.apply(static_cast<Derived const &>(*this));
    }

    /// \brief Creates and returns a deep copy of this node.
    ///
    /// This method provides a default implementation for cloning nodes.
    /// It requires that the `Derived` class is copy-constructible.
    /// The new node is allocated on the heap and returned as a `Ref<Node>`.
    ///
    /// \return A `Ref<Node>` pointing to the newly created clone.
    [[nodiscard]] Ref<Node> clone() const override {
        static_assert(std::is_copy_constructible_v<Derived>);
        return {new Derived(static_cast<Derived const &>(*this))};
    }

public:
    /// \brief Returns the human-readable name of the derived class type.
    ///
    /// This method provides runtime type information for the `Derived` class,
    /// primarily for debugging and logging purposes. It utilizes `getStaticTypeName`.
    ///
    /// \return A string containing the demangled (if available) name of the `Derived` class type.
    ///
    /// \note This method is intended for debugging and logging. Avoid using its output
    ///       for type-checking or dispatching logic in production code.
    [[nodiscard]] std::string getTypeName() const override {
        (void)(this);
        return getStaticTypeName();
    }

    /// \brief Returns the static, potentially mangled, type name of the Derived class.
    ///
    /// This helper function retrieves the type name using `typeid`. On GCC/Clang,
    /// it attempts to demangle the name for better readability.
    /// \return A string representing the type name of `Derived`.
    [[nodiscard]] static std::string getStaticTypeName() {
#if defined(__GNUC__)
        return details::demangle(typeid(Derived).name());
#else
        return typeid(Derived).name();
#endif
    }
};

/// \brief A mixin class that extends `NodeMixin` to add serialization capabilities.
///
/// `SerializableMixin` provides implementations for `toBytes` and `fromBytes`
/// using the `cereal` library.
///
/// \tparam Derived The concrete node class that inherits from this mixin.
///                It must provide a `archive` method.
/// \tparam Base The base class (usually `krd::Node` or a class derived from it)
///              that `Derived` would normally inherit from.
template <class Derived, class Base> class SerializableMixin : public NodeMixin<Derived, Base> {
public:
    /// \brief Serializes the object using the provided archive.
    ///
    /// This method calls the `archive` method of the derived class to serialize its members.
    ///
    /// \param ar The archive object as invoked by the cereal.
    void serialize(auto &ar) {
        KRD_ASSERT(
            isSerializable, "SerializableMixin: Derived class must implement the archive method."
        );
        if (isSerializable) {
            Archive<std::remove_cvref_t<decltype(ar)>> ar2{ar};
            static_cast<Derived &>(*this).archive(ar2);
        }
    }

public:
    /// \brief Serializes the node to a binary output stream.
    ///
    /// This method overrides `Node::toBytes` and uses `cereal` to serialize
    /// the `Derived` object into the provided output stream.
    ///
    /// \param os The output stream where the serialized data will be written.
    /// \throw cereal::Exception if serialization fails.
    void toBytes(std::ostream &os) override {
        cereal::BinaryOutputArchive ar(os);
        ar(static_cast<Derived &>(*this));
    }

    /// \brief Deserializes the node from a binary input stream.
    ///
    /// This method overrides `Node::fromBytes` and uses `cereal` to deserialize
    /// data from the provided input stream into the `Derived` object.
    ///
    /// \param is The input stream from which serialized data will be read.
    /// \throw cereal::Exception if deserialization fails.
    void fromBytes(std::istream &is) override {
        cereal::BinaryInputArchive ar(is);
        ar(static_cast<Derived &>(*this));
    }

private:
    /// Register the derived class for serialization.
    ///
    /// This static member is used to register the derived class with the `SerializableFactory`.
    /// The variable is used in \c serialize to ensure that this procedure is actually called.
    inline static bool isSerializable = [] { // NOLINT
        auto &factory = SerializableFactory::getInstance();

        auto typeHash = SerializableFactory::getTypeHash<Derived>();
        auto creator = []() -> Ref<Node> { return Derived::create().template cast<Node>(); };
        if (factory.registerNodeCreator(typeHash, std::move(creator))) {
            LogTrace(
                "SerializableMixin: Registered '{:s}' for serialization",
                Derived::getStaticTypeName()
            );
            return true;
        }

        LogTrace(
            "SerializableMixin: Failed to register '{:s}' serialization",
            Derived::getStaticTypeName()
        );
        return false;
    }();
};
} // namespace krd
