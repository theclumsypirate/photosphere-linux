#include "core/exposure.hpp"

#include <algorithm>
#include <cmath>

namespace ps {

std::vector<float> solveGains(const std::vector<double>& meanLuma,
                              const std::vector<std::pair<int, int>>& edges) {
    constexpr int kIterations = 12;
    constexpr double kMaxGain = 1.6;
    constexpr double kMinUsableLuma = 1e-6;
    const int n = static_cast<int>(meanLuma.size());
    std::vector<float> gains(n, 1.0f);
    if (n == 0) return gains;

    std::vector<bool> usable(n, false);
    for (int i = 0; i < n; ++i) usable[i] = meanLuma[i] > kMinUsableLuma;
    std::vector<int> degree(n, 0), offsets(n + 1, 0);
    for (const auto& e : edges) {
        if (e.first >= 0 && e.first < n && e.second >= 0 && e.second < n &&
            e.first != e.second && usable[e.first] && usable[e.second]) {
            ++degree[e.first];
            ++degree[e.second];
        }
    }
    for (int i = 0; i < n; ++i) offsets[i + 1] = offsets[i] + degree[i];
    std::vector<int> cursor = offsets;
    std::vector<int> neighbours(offsets[n]);
    for (const auto& e : edges) {
        if (e.first >= 0 && e.first < n && e.second >= 0 && e.second < n &&
            e.first != e.second && usable[e.first] && usable[e.second]) {
            neighbours[cursor[e.first]++] = e.second;
            neighbours[cursor[e.second]++] = e.first;
        }
    }
    std::vector<double> logLuma(n, 0.0), logGain(n, 0.0);
    for (int i = 0; i < n; ++i)
        if (usable[i]) logLuma[i] = std::log(meanLuma[i]);

    for (int it = 0; it < kIterations; ++it) {
        for (int i = 0; i < n; ++i) {
            const int start = offsets[i], end = offsets[i + 1];
            if (end <= start) continue;
            double sum = 0.0;
            for (int s = start; s < end; ++s) {
                const int j = neighbours[s];
                sum += logGain[j] + logLuma[j] - logLuma[i];
            }
            logGain[i] = sum / (end - start);
        }
    }
    double logSum = 0.0;
    int connected = 0;
    for (int i = 0; i < n; ++i) {
        if (offsets[i + 1] > offsets[i]) {
            logSum += logGain[i];
            ++connected;
        }
    }
    const double offset = connected > 0 ? logSum / connected : 0.0;
    const double maxLog = std::log(kMaxGain);
    for (int i = 0; i < n; ++i) {
        if (offsets[i + 1] <= offsets[i]) { gains[i] = 1.0f; continue; }
        gains[i] = static_cast<float>(std::exp(std::clamp(logGain[i] - offset,
                                                          -maxLog, maxLog)));
    }
    return gains;
}

} // namespace ps
