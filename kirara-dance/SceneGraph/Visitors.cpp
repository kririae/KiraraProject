#include "Visitors.h"

//
#include "Group.h"
#include "Node.h"

//
#include "Scene/Animation.h"
#include "Scene/Geometry.h"
#include "Scene/SceneRoot.h"
#include "Scene/Transform.h"

//
#include "FacadeRender/TriangleMeshResource.h"

namespace krd {
void Visitor::apply(Node &val) { (void)(val); }
void ConstVisitor::apply(Node const &val) { (void)(val); }

#define DEFAULT_VISIT_NODE(node)                                                                   \
    void Visitor::apply(node &val) { apply(static_cast<node::Parent &>(val)); }                    \
    void ConstVisitor::apply(node const &val) { apply(static_cast<node::Parent const &>(val)); }

DEFAULT_VISIT_NODE(Group)

DEFAULT_VISIT_NODE(Animation)
// DEFAULT_VISIT_NODE(SceneRoot)
DEFAULT_VISIT_NODE(Transform)
DEFAULT_VISIT_NODE(Geometry)
DEFAULT_VISIT_NODE(TriangleMesh)
DEFAULT_VISIT_NODE(TriangleMeshResource)
DEFAULT_VISIT_NODE(TransformAnimationChannel)

void Visitor::apply(SceneRoot &val) {
    {
        std::lock_guard lock(val.GSL);
        (void)(lock);
    }

    apply(static_cast<SceneRoot::Parent &>(val));
}

void ConstVisitor::apply(SceneRoot const &val) {
    {
        std::lock_guard lock(val.GSL);
        (void)(lock);
    }

    apply(static_cast<SceneRoot::Parent const &>(val));
}
} // namespace krd
