#pragma once

#include <cereal/types/unordered_map.hpp>

#include "Node.h"
#include "kira/SmallVector.h"

namespace krd {
template <typename T> class Ref;
class Node;

namespace detail {
template <typename T> struct IsRef : std::false_type {};
template <typename T> struct IsRef<Ref<T>> : std::true_type {};

void toStringStream(SerializationContext &ctx, std::stringstream &ss, Ref<Node> const &node);
void fromStringStream(SerializationContext &ctx, std::stringstream &ss, Ref<Node> const &node);
} // namespace detail

class SerializableFactory : public SingletonMixin<SerializableFactory> {
    SerializableFactory() = default;

public:
    friend class SingletonMixin<SerializableFactory>;
    using HashType = uint64_t;

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
    Ref<Node> createNode(HashType typeHash, Node::UUIDType const &uuid);

private:
    std::unordered_map<HashType, std::function<Ref<Node>()>> nodeCreators;
};

///
class SerializationContext : public std::unordered_map<Node::UUIDType, std::string> {};

///
template <typename IOArchive> class Archive {
public:
    Archive(SerializationContext &ctx, IOArchive &ar) : ctx(ctx), ar(ar) {}

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
        if constexpr (isSave()) {
            if (!arg) {
                LogTrace("Archive: Ref<T> is empty, not serialized");
                ar(false);
                return *this;
            }

            if (!arg->isSerializable()) {
                LogTrace("Archive: Node '{}' is not serializable", arg->getHumanReadable());
                ar(false);
                return *this;
            }

            ar(true); // indicate that the node is serializable

            auto const &uuid = arg->getUUID();
            ar(uuid);
            ar(arg->getTypeHash());

            // register the node into the context
            if (auto it = ctx.find(uuid); it == ctx.end()) {
                std::stringstream ss;
                detail::toStringStream(ctx, ss, arg);
                ctx.emplace(uuid, std::move(ss).str());
            }
        } else {
            bool isSerialized{false};
            ar(isSerialized);
            if (!isSerialized) {
                LogTrace("Archive: Node [unknown] is not serialized, cancel loading");
                return *this;
            }

            // get the UUID from the pool and restore it to arg
            // arg = ...
            uuids::uuid uuid;
            std::size_t typeHash;
            ar(uuid);
            ar(typeHash);

            if (auto *rawNode = NodeDedupManager::getInstance().getNodeByUUID(uuid); rawNode) {
                // ar(..) write to empty
                LogTrace(
                    "Archive: Node '{}' is already in the context", rawNode->getHumanReadable()
                );
                arg = Ref<Node>{rawNode}.dyn_cast<Type>();
            } else {
                auto newNode = SerializableFactory::getInstance().createNode(typeHash, uuid);
                if (!newNode) {
                    LogWarn(
                        "Archive: Failed to create node with UUID {} and type hash {}",
                        uuids::to_string(uuid), typeHash
                    );
                    return *this;
                }

                if (auto it = ctx.find(uuid); it != ctx.end()) {
                    std::stringstream ss{std::move(it->second)};
                    detail::fromStringStream(ctx, ss, newNode);
                    ctx.erase(it);
                } else {
                    LogWarn(
                        "Archive: Failed to find node with UUID {} and type hash {} "
                        "within the serialization context",
                        uuids::to_string(uuid), typeHash
                    );
                    return *this;
                }

                // Write back the node into the variable
                arg = Ref{newNode}.dyn_cast<Type>();
            }
        }

        return *this;
    }

private:
    SerializationContext &ctx;
    IOArchive &ar;
};
} // namespace krd

namespace cereal {
template <class T>
void save(auto &ar, kira::SmallVector<T> const &vec)
    requires(traits::is_output_serializable<BinaryData<T>, decltype(ar)>::value)
{
    ar(cereal::make_size_tag(vec.size()));
    for (auto const &v : vec)
        ar(v);
}

template <class T>
void load(auto &ar, kira::SmallVector<T> &vec)
    requires(traits::is_input_serializable<BinaryData<T>, decltype(ar)>::value)
{
    size_t size{0};
    ar(cereal::make_size_tag(size));
    vec.resize(size);
    for (auto &v : vec)
        ar(v);
}
} // namespace cereal
