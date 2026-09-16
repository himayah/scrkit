#include "ForegroundMeshBuilder.h"

#include <cmath>

namespace core::fx {

Mesh BuildForegroundRestMesh(const LayerSource& layer, const std::vector<float>& seamFractions) {
    std::vector<bool> activeCells(static_cast<size_t>(layer.gridN) * layer.gridN, false);
    for (int idx : layer.cellIndices) {
        if (idx >= 0 && static_cast<size_t>(idx) < activeCells.size()) activeCells[static_cast<size_t>(idx)] = true;
    }
    const int cellSubdiv = layer.gridN < 48 ? static_cast<int>(std::ceil(48.0 / layer.gridN)) : 1;
    const int cols = layer.gridN * cellSubdiv;

    std::vector<int> seamColumns;
    seamColumns.reserve(seamFractions.size());
    for (float f : seamFractions) seamColumns.push_back(static_cast<int>(std::lround(f * cols)));

    return MeshBuilder::BuildGrid(layer.screenW, layer.screenH, cols, cols, seamColumns, activeCells, cellSubdiv);
}

} // namespace core::fx
