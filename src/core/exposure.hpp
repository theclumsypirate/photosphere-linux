#pragma once

#include <utility>
#include <vector>

namespace ps {

// Brown/Lowe gain compensation in log space, ported from ExposureCompensation.kt.
// Edges list overlapping frame pairs in either order. Frames with no partner
// keep gain 1.
std::vector<float> solveGains(const std::vector<double>& meanLuma,
                              const std::vector<std::pair<int, int>>& edges);

} // namespace ps
