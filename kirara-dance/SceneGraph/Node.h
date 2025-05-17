#pragma once

#include <uuid.h>

#include <algorithm>
#include <mutex>
#include <range/v3/view/any_view.hpp>

#include "Core/Object.h"
#include "Visitors.h"

namespace krd {
class SerializationContext;
class SerializableFactory;

/// \brief Manager class that maintains a mapping between UUIDs and Node instances.
///
/// NodeDedupManager implements the singleton pattern and serves as a central registry
/// for all nodes in the scene graph.
///
/// The class ensures thread safety through mutex locking on all operations.
class NodeDedupManager {
public:
    /// \brief RAII wrapper for node registration/deregistration with the NodeDedupManager.
    ///
    /// This structure automatically registers a node when constructed and
    /// deregisters the node when destroyed, ensuring proper cleanup of the
    /// UUID-to-Node mapping even in case of exceptions.
    struct NodeRegistra {
        uuids::uuid const &uuid;

        /// \brief Registers a node with the given UUID in the NodeDedupManager.
        ///
        /// \param uuid The UUID to register the node under
        /// \param node Pointer to the node being registered
        explicit NodeRegistra(uuids::uuid const &uuid, Node *node) : uuid(uuid) {
            NodeDedupManager::getInstance().registerNodeByUUID(uuid, node);
        }

        /// \brief Changes the UUID of a node in the NodeDedupManager.
        void changeUUID(uuids::uuid const &oldUuid, uuids::uuid const &newUuid) {
            NodeDedupManager::getInstance().changeNodeUUID(oldUuid, newUuid);
        }

        /// \brief Deregisters the node from the NodeDedupManager upon destruction.
        ~NodeRegistra() { NodeDedupManager::getInstance().discardNodeByUUID(uuid); }
    };

public:
    /// \brief Returns the singleton instance of NodeDedupManager.
    ///
    /// This method implements the singleton pattern, ensuring only one instance
    /// of NodeDedupManager exists throughout the application's lifetime.
    ///
    /// \return Reference to the singleton instance
    [[nodiscard]] static NodeDedupManager &getInstance() {
        static NodeDedupManager instance;
        return instance;
    }

    /// \brief Retrieves a node by its UUID.
    ///
    /// Performs a thread-safe lookup of a node using its UUID.
    ///
    /// \param uuid The UUID of the node to retrieve
    /// \return Pointer to the node if found, nullptr otherwise
    [[nodiscard]] Node *getNodeByUUID(uuids::uuid const &uuid) {
        std::lock_guard lock(mutex);
        auto it = uuidMap.find(uuid);
        if (it != uuidMap.end())
            return it->second;
        return nullptr;
    }

    /// \brief Registers a node with the given UUID in the manager.
    ///
    /// If a node with the same UUID already exists, it will be replaced and
    /// a trace log message will be generated.
    ///
    /// \param uuid The UUID to register the node under
    /// \param node Pointer to the node being registered
    void registerNodeByUUID(uuids::uuid const &uuid, Node *node) {
        std::lock_guard lock(mutex);
        auto it = uuidMap.find(uuid);
        if (it == uuidMap.end())
            uuidMap.emplace(uuid, node);
        else {
            LogTrace("NodeDedupManager: Replacing node with UUID {}", uuids::to_string(uuid));
            it->second = node;
        }
    }

    /// \brief Removes a node registration for the given UUID.
    ///
    /// \param uuid The UUID of the node to deregister
    /// \return true if the node was found and removed, false otherwise
    bool discardNodeByUUID(uuids::uuid const &uuid) {
        std::lock_guard lock(mutex);
        auto it = uuidMap.find(uuid);
        if (it != uuidMap.end()) {
            uuidMap.erase(it);
            return true;
        }

        return false;
    }

    /// \brief Changes the UUID of a node in the manager.
    ///
    /// This is used only for serialization purposes.
    bool changeNodeUUID(uuids::uuid const &oldUuid, uuids::uuid const &newUuid) {
        std::lock_guard lock(mutex);
        auto it = uuidMap.find(oldUuid);
        if (it != uuidMap.end()) {
            auto *node = it->second;
            uuidMap.erase(it);
            uuidMap.emplace(newUuid, node);
            return true;
        }
        return false;
    }

private:
    std::mutex mutex;
    std::unordered_map<uuids::uuid, Node *> uuidMap;
};

/// \brief Base class for all nodes in the scene graph.
///
/// A Node represents an element within a hierarchical scene structure.
/// It supports operations like accepting visitors for traversal and inspection.
class Node : public Object {
public:
    friend class SerializableFactory;

    using UUIDType = uuids::uuid;

    Node &operator=(Node other) = delete;
    ~Node() override = default;

    void serialize(auto &ctx, auto &ar) {
        // do nothing
    }

    /// \brief Accepts a non-const visitor.
    ///
    /// This method is part of the visitor design pattern and allows a `Visitor`
    /// to operate on this node.
    /// \param visitor The non-const visitor to accept.
    virtual void accept(Visitor &visitor) { visitor.apply(*this); }

    /// \brief Accepts a const visitor.
    ///
    /// This method is part of the visitor design pattern and allows a `ConstVisitor`
    /// to operate on this node without modifying it.
    /// \param visitor The const visitor to accept.
    virtual void accept(ConstVisitor &visitor) const { visitor.apply(*this); }

    /// \brief Traverses the children of this node using a non-const visitor.
    ///
    /// This method returns a view of the direct children of this node. The visitor
    /// can use this view to continue traversal or gather information.
    /// The default implementation returns an empty view, indicating no children.
    ///
    /// \note The visitor is responsible for implementing the actual traversal logic
    ///       (e.g., depth-first, breadth-first) and should not modify the node itself
    ///       during this specific `traverse` call. Modifications should typically
    ///       happen within the visitor's `apply` methods.
    /// \param visitor The non-const visitor to guide the traversal.
    /// \return A `ranges::any_view` of `Ref<Node>` representing the children.
    [[nodiscard]] virtual ranges::any_view<Ref<Node>> traverse(Visitor &visitor) {
        (void)(visitor);
        return {};
    }

    /// \brief Traverses the children of this node using a const visitor.
    ///
    /// This method returns a view of the direct children of this node. The const visitor
    /// can use this view to continue traversal or gather information without modifying
    /// any part of the scene graph.
    /// The default implementation returns an empty view, indicating no children.
    ///
    /// \note The visitor is responsible for implementing the actual traversal logic.
    /// \param visitor The const visitor to guide the traversal.
    /// \return A `ranges::any_view` of `Ref<Node>` representing the children.
    [[nodiscard]] virtual ranges::any_view<Ref<Node>> traverse(ConstVisitor &visitor) const {
        (void)(visitor);
        return {};
    }

    /// \brief Clones the node.
    ///
    /// The returned node is a shallow copy of the original node.
    ///
    /// \remark The node is not cloned recursively, and might return a nullptr if cloned is not
    /// supported.
    [[nodiscard]] virtual Ref<Node> clone() const { return nullptr; }

#if 0
    /// \brief Replace a node with another node.
    ///
    /// If the node is not found, this function does nothing.
    /// The function is non-recursive thus does not traverse the children.
    virtual void replace(uint64_t oldId, Ref<Node> const &newNode) {}
#endif

    /// \brief Returns the unique identifier of the node.
    ///
    /// This ID is generated from a global atomic counter, ensuring uniqueness
    /// across all node instances. IDs are non-decreasing and are not reused.
    ///
    /// \remark This ID can be used to uniquely identify nodes, for example, in graph visualizations
    /// or debugging.
    [[nodiscard]] auto getId() const { return id; }

    /// \brief Returns the unique identifier of the node in the serialization context.
    ///
    /// This UUID is generated using a random number generator and is unique to this node instance.
    [[nodiscard]] auto getUUID() const { return uuid; }

    /// \brief Gets the specific type name of the node.
    ///
    /// This is a pure virtual function that must be implemented by derived classes
    /// to return a string representation of their type (e.g., "MeshNode", "LightNode").
    ///
    /// \see NodeMixin::getTypeName() for a potential helper mixin.
    /// \return A string representing the node's type.
    [[nodiscard]] virtual std::string getTypeName() const { return "Node"; }

    /// \brief Gets a human-readable string representation of the node.
    ///
    /// By default, this returns a string containing the node's type name and its unique ID.
    /// Derived classes can override this to provide more specific information.
    /// \return A human-readable string.
    [[nodiscard]] virtual std::string getHumanReadable() const {
        return std::format("[{} ({})]", getTypeName(), getId());
    }

    /// \brief A mutex to protect the node graph.
    ///
    /// The Global Node Lock (GNL) is used to protect the node from concurrent access.
    mutable std::mutex GNL;

public:
    virtual void toBytes(SerializationContext &, std::ostream &) {}
    virtual void fromBytes(SerializationContext &, std::istream &) {}

    /// Determines if the node can be serialized.
    virtual bool isSerializable() const { return false; }

    /// \brief Returns the type hash of the node.
    ///
    /// This method provides a unique hash code for the node type, which can be used
    /// for type identification and comparison.
    ///
    /// \remark Currently only Serializable nodes provide viable type hashes.
    [[nodiscard]] virtual uint64_t getTypeHash() const { return 0; }

protected:
    /// \brief Global atomic counter to generate unique node IDs.
    static inline std::atomic_uint64_t nodeCount;
    /// \brief The unique identifier for this node instance in the running context.
    uint64_t id{nodeCount.fetch_add(1)};
    /// \brief The unique identifier for this node instance in the serialization context.
    UUIDType uuid{genRandomUUID()};
    /// RAII the node registration process.
    NodeDedupManager::NodeRegistra reg{uuid, this};

    void updateUUID(uuids::uuid const &newUuid) {
        std::lock_guard lock(GNL);
        reg.changeUUID(this->uuid, newUuid);
        this->uuid = newUuid;
    }

    ///
    // uint64_t key{0};
    // uint64_t range{0};

    /// Nodes are typically not meant to be created directly but rather through derived classes
    /// or factory methods. This constructor initializes the unique `id` for the node
    /// by incrementing the global `nodeCount`.
    Node() = default;

    Node(UUIDType const &uuid) : uuid(uuid), reg(uuid, this) {}
    Node(Node const &other) : Object(other) {
        // The GSL member (std::mutex) is not copyable.
        // It will be default-initialized in the new Node object, meaning 'this->GSL' will be a new,
        // unlocked mutex.
    }

    // from https://github.com/mariusbancila/stduuid
    [[nodiscard]] static UUIDType genRandomUUID() {
        std::random_device rd;
        auto seedData = std::array<int, std::mt19937::state_size>{};
        std::ranges::generate(seedData, std::ref(rd));
        std::seed_seq seq(std::begin(seedData), std::end(seedData));
        std::mt19937 generator(seq);
        uuids::uuid_random_generator gen{generator};
        return gen();
    }
};
} // namespace krd

namespace fmt {
template <> struct formatter<uuids::uuid> {
    template <typename ParseContext> constexpr auto parse(ParseContext &ctx) { return ctx.begin(); }

    template <typename FormatContext>
    auto format(uuids::uuid const &uuid, FormatContext &ctx) const {
        return format_to(ctx.out(), "{}", uuids::to_string(uuid));
    }
};
} // namespace fmt

namespace cereal {
void save(auto &ar, ::krd::Node::UUIDType const &id) { ar(binary_data(&id, sizeof(id))); }
void load(auto &ar, ::krd::Node::UUIDType &id) { ar(binary_data(&id, sizeof(id))); }
} // namespace cereal
