#include "flux/Scene/TriangleMesh.h"

#include <charconv>
#include <cstdint>
#include <fstream>
#include <limits>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>

#include "kira/Anyhow.h"

namespace flux {
namespace {
[[nodiscard]] std::uint32_t parseVertexIndex(
    std::string_view token, std::filesystem::path const &path, std::size_t lineNumber
) {
    token = token.substr(0, token.find('/'));

    std::uint64_t index = 0;
    auto const [end, error] = std::from_chars(token.data(), token.data() + token.size(), index);
    if (error != std::errc{} || end != token.data() + token.size() || index == 0 ||
        index > std::numeric_limits<std::uint32_t>::max())
        throw kira::Anyhow(
            "TriangleMesh: invalid vertex index '{}' at {}:{}", token, path.string(), lineNumber
        );
    return static_cast<std::uint32_t>(index - 1);
}
} // namespace

TriangleMesh::TriangleMesh(TXContext &tx, kira::Properties properties)
    : RenderObject(tx, std::move(properties)) {
    loadObj(getProperties().use<std::filesystem::path>("path"));
}

// ponytail: This parser intentionally accepts only positive triangular OBJ
// faces; replace it when production ingestion needs a broader OBJ dialect.
void TriangleMesh::loadObj(std::filesystem::path const &path) {
    std::ifstream input(path);
    if (!input)
        throw kira::Anyhow("TriangleMesh: failed to open OBJ '{}'", path.string());

    std::string line;
    std::size_t lineNumber = 0;
    while (std::getline(input, line)) {
        ++lineNumber;
        std::istringstream fields(line);
        std::string tag;
        fields >> tag;
        if (tag.empty() || tag.starts_with('#'))
            continue;

        if (tag == "v") {
            float x = 0.0F;
            float y = 0.0F;
            float z = 0.0F;
            if (!(fields >> x >> y >> z))
                throw kira::Anyhow(
                    "TriangleMesh: invalid vertex at {}:{}", path.string(), lineNumber
                );
            vertices_.emplace_back(x, y, z);
            continue;
        }

        if (tag == "f") {
            std::string a;
            std::string b;
            std::string c;
            std::string extra;
            if (!(fields >> a >> b >> c) || (fields >> extra && !extra.starts_with('#')))
                throw kira::Anyhow(
                    "TriangleMesh: only triangular OBJ faces are supported at {}:{}", path.string(),
                    lineNumber
                );
            triangles_.emplace_back(
                parseVertexIndex(a, path, lineNumber), parseVertexIndex(b, path, lineNumber),
                parseVertexIndex(c, path, lineNumber)
            );
        }
    }

    if (vertices_.empty() || triangles_.empty())
        throw kira::Anyhow("TriangleMesh: OBJ '{}' contains no triangles", path.string());

    for (auto const &triangle : triangles_)
        for (auto const index : triangle)
            if (index >= vertices_.size())
                throw kira::Anyhow(
                    "TriangleMesh: vertex index {} exceeds vertex count {} in '{}'", index,
                    vertices_.size(), path.string()
                );
}
} // namespace flux
