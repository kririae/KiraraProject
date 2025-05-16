#pragma once

#include <uuid.h>

#include <cereal/cereal.hpp>

#include "Node.h"

namespace krd {
template <typename T> class Ref;
class Node;

namespace detail {
template <typename T> struct IsRef : std::false_type {};
template <typename T> struct IsRef<Ref<T>> : std::true_type {};
} // namespace detail

class SerializableFactory {
public:
    using HashType = std::size_t;

    /// Create a static instance of the SerializableFactory.
    [[nodiscard]] static SerializableFactory &getInstance() {
        static SerializableFactory instance;
        return instance;
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
        if (!arg->isSerializable()) {
            LogWarn("SerializableMixin: Node is not serializable");
            return *this;
        }

        if constexpr (isSave()) {
            ar(arg.getUUID());
            ar(arg.getTypeHash());

            // auto buffer = arg->toBytes();
            // ar(buffer);
        } else {
            // get the UUID from the pool and restore it to arg
            // arg = ...
            uuids::uuid uuid;
            std::size_t typeHash;
            ar(uuid);
            ar(typeHash);

            if (auto *rawNode = NodeDedupManager::getInstance().getNodeByUUID(uuid); rawNode) {
                // ar(..) write to empty
                arg = Ref<Node>{rawNode}.dyn_cast<Type>();
            } else {
                //
                auto newNode = SerializableFactory::getInstance().createNode(typeHash);
            }
        }

        return *this;
    }

private:
    IOArchive &ar;
};
} // namespace krd
