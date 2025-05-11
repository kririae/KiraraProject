#include "Visitors.h"

//
#include "Group.h"
#include "Node.h"

//
#include "Scene2/Geometry.h"
#include "Scene2/SceneRoot.h"
#include "Scene2/Transform.h"

//
#include "ImmediateRender/TriangleMeshResource.h"

namespace krd {
void Visitor::apply(Node &val) { (void)(val); }
void ConstVisitor::apply(Node const &val) { (void)(val); }

#define DEFAULT_VISIT_NODE(node)                                                                   \
    void Visitor::apply(node &val) { apply(static_cast<node::Parent &>(val)); }                    \
    void ConstVisitor::apply(node const &val) { apply(static_cast<node::Parent const &>(val)); }

DEFAULT_VISIT_NODE(Group)
DEFAULT_VISIT_NODE(SceneRoot)
DEFAULT_VISIT_NODE(Transform)
DEFAULT_VISIT_NODE(Geometry)
DEFAULT_VISIT_NODE(TriangleMesh)
DEFAULT_VISIT_NODE(TriangleMeshResource)
} // namespace krd
