#pragma once

#include <cereal/cereal.hpp>

namespace krd {
template <typename T> class Ref;
class Node;

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

class SerializableFactory {
public:
    using HashType = std::size_t;

    /// Create a static instance of the SerializableFactory.
    [[nodiscard]] static SerializableFactory &getInstance() {
        static SerializableFactory instance;
        return instance;
    }

    /// Get the hash code of the type to be registered.
    template <typename T>
    [[nodiscard]] static auto getTypeHash() -> HashType //
    {
        static_assert(std::is_same_v<decltype(typeid(T).hash_code()), HashType>);
        return typeid(T).hash_code();
    }

public:
    /// \brief Registers a node creator function with the given type hash.
    ///
    /// \param typeHash The hash code of the type to be registered.
    /// \param creator The function that creates the node.
    ///
    /// \return True if the registration was successful, false otherwise.
    bool registerNodeCreator(HashType typeHash, std::function<Ref<Node>()> creator);

    /// \brief Creates a node with the given type hash.
    ///
    /// \return A reference to the created node, non-null if successful.
    Ref<Node> createNode(HashType typeHash);

private:
    std::unordered_map<HashType, std::function<Ref<Node>()>> nodeCreators;
};
} // namespace krd
