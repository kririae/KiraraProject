#pragma once

#include "Visitors.h"

#if defined(__GNUC__)
#include <cxxabi.h>
#endif

namespace krd {
#if defined(__GNUC__)
namespace details {
// from https://stackoverflow.com/questions/12877521/human-readable-type-info-name
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
/// NodeMixin implements the visitor pattern for derived scene graph nodes.
/// It provides type-safe visitor dispatching by downcasting to the derived type.
///
/// \tparam Derived The derived class that inherits from this mixin.
/// \tparam Base The base class this mixin extends.
template <class Derived, class Base> class NodeMixin : public Base {
public:
    /// Type alias for the parent class
    using Parent = Base;

    /// \brief Accepts a visitor and dispatches to the appropriate visit method.
    ///
    /// This method implements the double-dispatch mechanism of the visitor pattern
    /// by down casting the current object to its concrete type and passing it to the visitor.
    ///
    /// \param visitor The visitor object that will process this node.
    void accept(Visitor &visitor) override {
        // Generate the derived class visitor application
        visitor.apply(static_cast<Derived &>(*this));
    }

    /// \brief Accepts a const visitor and dispatches to the appropriate visit method.
    ///
    /// Const version of the accept method that works with const visitors.
    ///
    /// \param visitor The const visitor object that will process this node.
    void accept(ConstVisitor &visitor) const override {
        /// Generate the const derived class visitor application
        visitor.apply(static_cast<Derived const &>(*this));
    }

public:
    /// \brief Returns the human-readable name of the derived class type.
    ///
    /// Provides runtime type information of the derived class for debugging purposes.
    /// Uses compiler-specific demangling when available.
    ///
    /// \return A string containing the name of the derived class type.
    ///
    /// \note This method is intended for debugging and logging purposes only. Do not use it to
    /// distinguish between different node types in production code.
    [[nodiscard]] std::string getTypeName() const override {
        (void)(this);
#if defined(__GNUC__)
        return details::demangle(typeid(Derived).name());
#else
        return typeid(Derived).name();
#endif
    }
};
} // namespace krd
