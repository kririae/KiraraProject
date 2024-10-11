#include "Scene/SceneBuilder.h"

#include <assimp/postprocess.h>
#include <assimp/scene.h>

#include <assimp/Importer.hpp>

namespace krd {
void SceneBuilder::loadFromFile(std::filesystem::path const &path) {
    Assimp::Importer importer;

    aiScene const *scene = importer.ReadFile(path.string(), aiProcess_Triangulate);
    if (!scene || !scene->mRootNode || scene->mFlags & AI_SCENE_FLAGS_INCOMPLETE)
        throw kira::Anyhow(
            "SceneBuilder: Failed to load the scene from '{:s}': {:s}", path.string(),
            importer.GetErrorString()
        );


}
} // namespace krd
