#pragma once

#include "kira/Compiler.h"

KIRA_DIAGNOSTIC_PUSH
KIRA_IGNORE_UNUSED_PARAMETER
KIRA_IGNORE_TYPE_LIMITS
#include <rfl/json.hpp>
KIRA_DIAGNOSTIC_POP

namespace kira {
namespace detail {
namespace rfl = ::rfl::json;
} // namespace detail

//! \note As for the `Core` module, reflection is enabled for
//! - `Anyhow`
//! - `FileResolver`
//! - `SmallVector`
//! Just use the `Serialize` and `Deserialize` functions to serialize and deserialize objects.
//!
//! \example
//! \code
//! SmallVector<int> svec{1, 2, 3};
//! auto str = Serialize(svec);
//! \endcode
//!
//! \see Serialize
//! \see Deserialize

/// Write a serializable object to a string.
template <typename T> decltype(auto) Serialize(T &&value) {
    return detail::rfl::write(std::forward<T>(value));
}

/// Read a serializable object from a string.
template <typename T> decltype(auto) Deserialize(std::string const &archive) {
    return detail::rfl::read<T>(archive);
}
} // namespace kira
