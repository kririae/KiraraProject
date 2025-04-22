#include "Visitors.h"

#include "Group.h"
#include "Node.h"
#include "Scene2/Geometry.h"
#include "Scene2/SceneRoot.h"
#include "Scene2/Transform.h"

namespace krd {
void Visitor::apply(Node &val) { (void)(val); }
void Visitor::apply(Group &val) { apply(static_cast<Node &>(val)); }

void Visitor::apply(SceneRoot &val) { apply(static_cast<Node &>(val)); }
void Visitor::apply(Transform &val) { apply(static_cast<Node &>(val)); }
void Visitor::apply(Geometry &val) { apply(static_cast<Transform &>(val)); }

void ConstVisitor::apply(Node const &val) { (void)(val); }
void ConstVisitor::apply(Group const &val) { apply(static_cast<Node const &>(val)); }

void ConstVisitor::apply(SceneRoot const &val) { apply(static_cast<Node const &>(val)); }
void ConstVisitor::apply(Transform const &val) { apply(static_cast<Node const &>(val)); }
void ConstVisitor::apply(Geometry const &val) { apply(static_cast<Transform const &>(val)); }
} // namespace krd
