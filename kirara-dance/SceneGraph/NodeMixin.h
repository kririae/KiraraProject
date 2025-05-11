#pragma once

#include "Visitors.h"

#if defined(__clang__)
#include <cxxabi.h>
#endif

namespace krd {
#if defined(__clang__)
namespace details {
// from https://stackoverflow.com/questions/12877521/human-readable-type-info-name
inline std::string demangle(char const *mangled) {
    int status;
    std::unique_ptr<char[], void (*)(void *)> result(
        abi::__cxa_demangle(mangled, 0, 0, &status), std::free
    );
    return result.get() ? std::string(result.get()) : "error occurred";
}
#endif
} // namespace details

template <class Derived, class Base> class NodeMixin : public Base {
public:
    // (1)
    void accept(Visitor &visitor) override {
        // Generate the derived class visitor application
        visitor.apply(static_cast<Derived &>(*this));
    }
    // (2)
    void accept(ConstVisitor &visitor) const override {
        /// Generate the const derived class visitor application
        visitor.apply(static_cast<Derived const &>(*this));
    }

public:
    /// \brief Returns the type name of the class
    ///
    /// \remark This is used for debugging and logging purposes, please dont use it to distinguish
    /// the different nodes.
    [[nodiscard]] std::string getTypeName() const override {
        (void)(this);
#if defined(__clang__)
        return details::demangle(typeid(Derived).name());
#else
        return typeid(Derived).name();
#endif
    }
};
} // namespace krd
