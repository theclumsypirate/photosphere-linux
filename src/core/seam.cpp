#include "core/seam.hpp"

#include <algorithm>
#include <cmath>

namespace ps {

float SeamWeights::weightFor(int frameIndex, int levelRow, int levelCol,
                             int sourceScale) const {
    if (labelMap.empty()) return 0.0f;
    const int row0 = levelRow * sourceScale;
    const int col0 = levelCol * sourceScale;
    const int gr = std::clamp(row0 / scale, 0, gridHeight - 1);
    const int gc = std::clamp(col0 / scale, 0, gridWidth - 1);
    const size_t index = static_cast<size_t>(gr) * gridWidth + gc;
    if (labelMap[index] == frameIndex) return 1.0f;
    if (loserLabel[index] == frameIndex) return loserWeight[index];
    return 0.0f;
}

std::pair<std::vector<int>, std::vector<float>> deriveSeamFeather(
    const std::vector<int>& labelMap, int gridWidth, int gridHeight,
    int halfWidth, bool wrapLongitude) {
    const size_t size = static_cast<size_t>(gridWidth) * gridHeight;
    std::vector<int> loserLabel(size, -1);
    std::vector<float> loserWeight(size, 0.0f);
    for (int r = 0; r < gridHeight; ++r) {
        for (int c = 0; c < gridWidth; ++c) {
            const size_t index = static_cast<size_t>(r) * gridWidth + c;
            const int winner = labelMap[index];
            if (winner < 0) continue;
            int bestLabel = -1;
            int bestDistance = halfWidth + 1;
            const int rowMin = std::max(0, r - halfWidth);
            const int rowMax = std::min(gridHeight - 1, r + halfWidth);
            for (int nr = rowMin; nr <= rowMax; ++nr) {
                for (int dc = -halfWidth; dc <= halfWidth; ++dc) {
                    int nc = c + dc;
                    if (wrapLongitude) {
                        nc = ((nc % gridWidth) + gridWidth) % gridWidth;
                    } else if (nc < 0 || nc >= gridWidth) {
                        continue;
                    }
                    const int neighbour = labelMap[static_cast<size_t>(nr) * gridWidth + nc];
                    if (neighbour < 0 || neighbour == winner) continue;
                    const int distance = std::max(std::abs(nr - r), std::abs(dc));
                    if (distance < bestDistance) {
                        bestDistance = distance;
                        bestLabel = neighbour;
                    }
                }
            }
            if (bestLabel >= 0) {
                loserLabel[index] = bestLabel;
                loserWeight[index] =
                    0.5f * (1.0f - static_cast<float>(bestDistance) / (halfWidth + 1.0f));
            }
        }
    }
    return {loserLabel, loserWeight};
}

} // namespace ps
