#pragma once

#include <cereal/cereal.hpp>

namespace krd {
template <typename T> class Ref;

namespace detail {
template <typename T> struct IsRef : std::false_type {};
template <typename T> struct IsRef<Ref<T>> : std::true_type {};
} // namespace detail

///
template <typename IOArchive> class Archive {
public:
    Archive(IOArchive &ar) : ar(ar) {}

    /// Returns true if the archive is in loading mode.
    static constexpr bool isLoad() { return IOArchive::is_loading::value; }
    /// Returns true if the archive is in saving mode.
    static constexpr bool isSave() { return IOArchive::is_saving::value; }

public:
    /// \brief Calls the archive function for each argument.
    ///
    /// \param args The arguments to be serialized or deserialized.
    /// \return The archive object.
    template <class... Types>
    Archive &operator()(Types &&...args)
        requires(sizeof...(Types) >= 2)
    {
        (this->operator()(std::forward<Types>(args)), ...);
        return *this;
    }

    /// \brief Calls the archive function for a single argument.
    ///
    /// \param arg The argument to be serialized or deserialized.
    template <class Type> Archive &operator()(Type &&arg) {
        ar(std::forward<Type>(arg));
        return *this;
    }

    /// \brief Calls the archive function for a Ref object.
    template <class Type> Archive &operator()(Ref<Type> &arg) {
        // do nothing for now
        return *this;
    }

private:
    IOArchive &ar;
};
} // namespace krd
