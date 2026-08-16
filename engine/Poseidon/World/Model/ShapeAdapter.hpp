#pragma once

#include <string>

namespace Poseidon { class LODShapeWithShadow; }
using Poseidon::LODShapeWithShadow;

namespace Poseidon {
namespace Model {

struct Model;

namespace ShapeAdapter
{
    struct ProxyModelName
    {
        std::string modelName;
        int id = -1;
    };

    ProxyModelName normalizeProxyModelName(const std::string& selectionName);
    LODShapeWithShadow* convertToLODShape(const Model& model, bool reversed = false);
}

} // namespace Model
} // namespace Poseidon
