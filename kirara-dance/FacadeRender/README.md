# Immediate Render

For simplicity, we directly use the original scene graph. For a simple real-time renderer, we don't have many extra resources to maintain.

DeserializeGraph DG{res};

Ref<Node> DeserializeGraph::execute() {
    auto root = SceneRoot::create();
    root->deserialize(DC);
    return root;
}

void Root::deserialize(DeserializeContext& DC) {
    // 1. Deserialize the general information
    root.setUUID(DC.getUUID());
    root.child1 = DC.getDG().execute(*this);
}

// type_name, UUID, Archive, vec<{UUID}>
