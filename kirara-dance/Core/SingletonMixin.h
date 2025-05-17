#pragma once

#include "kira/Macros.h"

namespace krd {
template <typename Derived> class SingletonMixin {
public:
    KIRA_DISALLOW_COPY_AND_ASSIGN(SingletonMixin);

    /// \brief Returns a reference to the singleton instance of the derived class.
    [[nodiscard]] static Derived &getInstance() {
        static Derived instance;
        return instance;
    }

protected:
    SingletonMixin() = default;
};
} // namespace krd
