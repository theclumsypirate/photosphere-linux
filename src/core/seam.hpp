#pragma once

#include <utility>
#include <vector>

namespace ps {

// Per-grid-cell frame ownership with a narrow feather across each cut.
// Ported from SeamWeights.kt and SeamFeather.kt.
struct SeamWeights {
    int gridWidth = 0;
    int gridHeight = 0;
    int scale = 1;
    std::vector<int> labelMap;
    std::vector<int> loserLabel;
    std::vector<float> loserWeight;

    float weightFor(int frameIndex, int levelRow, int levelCol, int sourceScale) const;
    bool empty() const { return labelMap.empty(); }
};

// Derives the loser labels and feather weights from a winner label map.
std::pair<std::vector<int>, std::vector<float>> deriveSeamFeather(
    const std::vector<int>& labelMap, int gridWidth, int gridHeight,
    int halfWidth, bool wrapLongitude = false);

} // namespace ps
