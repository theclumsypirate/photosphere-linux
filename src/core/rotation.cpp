#include "core/rotation.hpp"

#include <algorithm>
#include <cmath>
#include <numeric>

namespace ps::rotation {
namespace {

Quat add4(const Quat& a, const Quat& b) {
    return {a[0] + b[0], a[1] + b[1], a[2] + b[2], a[3] + b[3]};
}
Quat scale4(const Quat& a, double s) {
    return {a[0] * s, a[1] * s, a[2] * s, a[3] * s};
}
double dot4(const Quat& a, const Quat& b) {
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2] + a[3] * b[3];
}
double norm4(const Quat& a) { return std::sqrt(std::max(0.0, dot4(a, a))); }

double median(std::vector<double> v) {
    if (v.empty()) return 0.0;
    std::sort(v.begin(), v.end());
    const size_t n = v.size();
    return (n & 1) ? v[n / 2] : 0.5 * (v[n / 2 - 1] + v[n / 2]);
}

double residual(const std::vector<int>& inliers, const Mat3& r,
                const std::vector<Vec3>& from, const std::vector<Vec3>& to) {
    std::vector<double> errors;
    errors.reserve(inliers.size());
    for (int i : inliers) {
        const Vec3 a = apply(r, from[i]);
        errors.push_back(std::acos(std::clamp(dot3(a, to[i]), -1.0, 1.0)));
    }
    return median(std::move(errors));
}

} // namespace

Mat3 identity() { return {1, 0, 0, 0, 1, 0, 0, 0, 1}; }

Mat3 multiply(const Mat3& a, const Mat3& b) {
    Mat3 out{};
    for (int i = 0; i < 3; ++i)
        for (int j = 0; j < 3; ++j)
            out[i * 3 + j] = a[i * 3] * b[j] + a[i * 3 + 1] * b[3 + j] +
                             a[i * 3 + 2] * b[6 + j];
    return out;
}

Mat3 transpose(const Mat3& m) {
    return {m[0], m[3], m[6], m[1], m[4], m[7], m[2], m[5], m[8]};
}

Vec3 apply(const Mat3& m, const Vec3& v) {
    return {m[0] * v[0] + m[1] * v[1] + m[2] * v[2],
            m[3] * v[0] + m[4] * v[1] + m[5] * v[2],
            m[6] * v[0] + m[7] * v[1] + m[8] * v[2]};
}

double angle(const Mat3& a, const Mat3& b) {
    const Mat3 relative = multiply(transpose(a), b);
    const double cosine = std::clamp((relative[0] + relative[4] + relative[8] - 1.0) / 2.0,
                                     -1.0, 1.0);
    const double s1 = relative[7] - relative[5];
    const double s2 = relative[2] - relative[6];
    const double s3 = relative[3] - relative[1];
    const double sine = 0.5 * std::sqrt(s1 * s1 + s2 * s2 + s3 * s3);
    return std::atan2(sine, cosine);
}

Mat3 rotationAboutAxis(const Vec3& axis, double a) {
    const double x = axis[0], y = axis[1], z = axis[2];
    const double c = std::cos(a), s = std::sin(a), t = 1.0 - c;
    return {t * x * x + c, t * x * y - s * z, t * x * z + s * y,
            t * x * y + s * z, t * y * y + c, t * y * z - s * x,
            t * x * z - s * y, t * y * z + s * x, t * z * z + c};
}

Mat3 rotationBetween(const Vec3& from, const Vec3& to) {
    const Vec3 f = normalize3(from);
    const Vec3 t = normalize3(to);
    const double cosine = std::clamp(dot3(f, t), -1.0, 1.0);
    if (cosine > 1.0 - 1e-12) return identity();
    const Vec3 axis = cross3(f, t);
    const double n = norm3(axis);
    if (n < 1e-12) {
        const Vec3 seed = std::abs(f[0]) < 0.9 ? Vec3{1, 0, 0} : Vec3{0, 1, 0};
        return rotationAboutAxis(normalize3(cross3(f, seed)), M_PI);
    }
    return rotationAboutAxis(scale3(axis, 1.0 / n), std::acos(cosine));
}

Mat3 basisRotation(const Vec3& a1, const Vec3& b1, const Vec3& a2, const Vec3& b2) {
    const Vec3 u1 = normalize3(a1);
    const Vec3 u2 = normalize3(sub3(a2, scale3(u1, dot3(a2, u1))));
    const Vec3 u3 = cross3(u1, u2);
    const Vec3 v1 = normalize3(b1);
    const Vec3 v2 = normalize3(sub3(b2, scale3(v1, dot3(b2, v1))));
    const Vec3 v3 = cross3(v1, v2);
    Mat3 out{};
    for (int i = 0; i < 3; ++i)
        for (int j = 0; j < 3; ++j)
            out[i * 3 + j] = v1[i] * u1[j] + v2[i] * u2[j] + v3[i] * u3[j];
    return out;
}

Mat3 procrustes(const std::vector<Vec3>& a, const std::vector<Vec3>& b) {
    if (a.size() != b.size() || a.empty()) return identity();
    Mat3 h{};
    for (size_t i = 0; i < a.size(); ++i)
        for (int r = 0; r < 3; ++r)
            for (int c = 0; c < 3; ++c)
                h[r * 3 + c] += b[i][r] * a[i][c];

    // Jacobi eigen decomposition of H^T H. The eigenvector for the largest
    // eigenvalue gives the optimal rotation through polar decomposition.
    Mat3 s{};
    for (int i = 0; i < 3; ++i)
        for (int j = 0; j < 3; ++j)
            for (int k = 0; k < 3; ++k)
                s[i * 3 + j] += h[k * 3 + i] * h[k * 3 + j];

    Mat3 v = identity();
    for (int sweep = 0; sweep < 48; ++sweep) {
        int p = 0, q = 1;
        double best = 0.0;
        for (int i = 0; i < 3; ++i)
            for (int j = i + 1; j < 3; ++j)
                if (std::abs(s[i * 3 + j]) > best) {
                    best = std::abs(s[i * 3 + j]);
                    p = i; q = j;
                }
        if (best < 1e-14) break;
        const double theta = 0.5 * std::atan2(2.0 * s[p * 3 + q],
                                              s[q * 3 + q] - s[p * 3 + p]);
        const double c = std::cos(theta), sn = std::sin(theta);
        for (int k = 0; k < 3; ++k) {
            const double akp = s[k * 3 + p], akq = s[k * 3 + q];
            s[k * 3 + p] = c * akp - sn * akq;
            s[k * 3 + q] = sn * akp + c * akq;
        }
        for (int k = 0; k < 3; ++k) {
            const double apk = s[p * 3 + k], aqk = s[q * 3 + k];
            s[p * 3 + k] = c * apk - sn * aqk;
            s[q * 3 + k] = sn * apk + c * aqk;
        }
        for (int k = 0; k < 3; ++k) {
            const double vkp = v[k * 3 + p], vkq = v[k * 3 + q];
            v[k * 3 + p] = c * vkp - sn * vkq;
            v[k * 3 + q] = sn * vkp + c * vkq;
        }
    }
    // S = H^T H = V diag(lambda) V^T. The polar factor is
    // R = H (S)^(-1/2) = H V diag(1/sqrt(lambda)) V^T.
    Mat3 invSqrt{};
    for (int i = 0; i < 3; ++i) {
        const double lambda = std::max(1e-12, s[i * 3 + i]);
        const double w = 1.0 / std::sqrt(lambda);
        for (int k = 0; k < 3; ++k)
            for (int j = 0; j < 3; ++j)
                invSqrt[k * 3 + j] += v[k * 3 + i] * w * v[j * 3 + i];
    }
    return multiply(h, invSqrt);
}

Quat matrixToQuaternion(const Mat3& m) {
    const double trace = m[0] + m[4] + m[8];
    if (trace > 0.0) {
        const double s = 0.5 / std::sqrt(trace + 1.0);
        return {0.25 / s, (m[7] - m[5]) * s, (m[2] - m[6]) * s, (m[3] - m[1]) * s};
    }
    if (m[0] > m[4] && m[0] > m[8]) {
        const double s = 2.0 * std::sqrt(1.0 + m[0] - m[4] - m[8]);
        return {(m[7] - m[5]) / s, 0.25 * s, (m[1] + m[3]) / s, (m[2] + m[6]) / s};
    }
    if (m[4] > m[8]) {
        const double s = 2.0 * std::sqrt(1.0 + m[4] - m[0] - m[8]);
        return {(m[2] - m[6]) / s, (m[1] + m[3]) / s, 0.25 * s, (m[5] + m[7]) / s};
    }
    const double s = 2.0 * std::sqrt(1.0 + m[8] - m[0] - m[4]);
    return {(m[3] - m[1]) / s, (m[2] + m[6]) / s, (m[5] + m[7]) / s, 0.25 * s};
}

Mat3 quaternionToMatrix(const Quat& q) {
    const double w = q[0], x = q[1], y = q[2], z = q[3];
    return {1 - 2 * (y * y + z * z), 2 * (x * y - w * z), 2 * (x * z + w * y),
            2 * (x * y + w * z), 1 - 2 * (x * x + z * z), 2 * (y * z - w * x),
            2 * (x * z - w * y), 2 * (y * z + w * x), 1 - 2 * (x * x + y * y)};
}

Mat3 weightedMeanRotation(const std::vector<Mat3>& rotations,
                          const std::vector<double>& weights) {
    if (rotations.empty()) return identity();
    const Quat reference = matrixToQuaternion(rotations[0]);
    Quat sum{0, 0, 0, 0};
    for (size_t i = 0; i < rotations.size(); ++i) {
        Quat q = matrixToQuaternion(rotations[i]);
        if (dot4(q, reference) < 0.0) q = scale4(q, -1.0);
        sum = add4(sum, scale4(q, weights[i]));
    }
    const double magnitude = norm4(sum);
    if (magnitude < 1e-9) return rotations[0];
    return quaternionToMatrix(scale4(sum, 1.0 / magnitude));
}

std::vector<Mat3> averageRotations(const std::vector<Mat3>& initial,
                                   const std::vector<RotationEdge>& edges,
                                   int iterations) {
    std::vector<Mat3> current = initial;
    for (int it = 0; it < iterations; ++it) {
        for (size_t i = 1; i < current.size(); ++i) {
            std::vector<Mat3> estimates;
            std::vector<double> weights;
            for (const auto& e : edges) {
                if (static_cast<size_t>(e.from) == i) {
                    estimates.push_back(multiply(current[e.to], e.rotation));
                    weights.push_back(e.weight);
                } else if (static_cast<size_t>(e.to) == i) {
                    estimates.push_back(multiply(current[e.from], transpose(e.rotation)));
                    weights.push_back(e.weight);
                }
            }
            if (!estimates.empty()) current[i] = weightedMeanRotation(estimates, weights);
        }
    }
    return current;
}

RotationEstimate estimateRotation(const std::vector<Vec3>& from,
                                  const std::vector<Vec3>& to,
                                  double thresholdRadians, int maxIterations,
                                  std::mt19937& rng) {
    RotationEstimate best;
    bool haveBest = false;
    if (from.size() < 2) return best;
    std::uniform_int_distribution<size_t> pick(0, from.size() - 1);
    for (int iter = 0; iter < maxIterations; ++iter) {
        const size_t first = pick(rng);
        size_t second = pick(rng);
        if (second == first) continue;
        const Mat3 candidate = basisRotation(from[first], to[first], from[second], to[second]);
        std::vector<int> inliers;
        for (size_t i = 0; i < from.size(); ++i) {
            const Vec3 a = apply(candidate, from[i]);
            if (std::acos(std::clamp(dot3(a, to[i]), -1.0, 1.0)) <= thresholdRadians)
                inliers.push_back(static_cast<int>(i));
        }
        if (inliers.size() < 2) continue;
        if (!haveBest || inliers.size() > best.inliers.size()) {
            best.rotation = candidate;
            best.inliers = std::move(inliers);
            best.residualRadians = residual(best.inliers, candidate, from, to);
            haveBest = true;
        }
    }
    if (!haveBest) return {};
    std::vector<Vec3> a, b;
    a.reserve(best.inliers.size());
    b.reserve(best.inliers.size());
    for (int i : best.inliers) { a.push_back(from[i]); b.push_back(to[i]); }
    const Mat3 refit = procrustes(a, b);
    std::vector<int> refined;
    for (size_t i = 0; i < from.size(); ++i) {
        const Vec3 v = apply(refit, from[i]);
        if (std::acos(std::clamp(dot3(v, to[i]), -1.0, 1.0)) <= thresholdRadians)
            refined.push_back(static_cast<int>(i));
    }
    best.rotation = refit;
    best.inliers = std::move(refined);
    best.residualRadians = residual(best.inliers, refit, from, to);
    return best;
}

double dot3(const Vec3& a, const Vec3& b) {
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
}
Vec3 cross3(const Vec3& a, const Vec3& b) {
    return {a[1] * b[2] - a[2] * b[1],
            a[2] * b[0] - a[0] * b[2],
            a[0] * b[1] - a[1] * b[0]};
}
double norm3(const Vec3& a) { return std::sqrt(dot3(a, a)); }
Vec3 normalize3(const Vec3& a) {
    const double n = norm3(a);
    return n > 1e-12 ? scale3(a, 1.0 / n) : a;
}
Vec3 scale3(const Vec3& a, double s) { return {a[0] * s, a[1] * s, a[2] * s}; }
Vec3 add3(const Vec3& a, const Vec3& b) {
    return {a[0] + b[0], a[1] + b[1], a[2] + b[2]};
}
Vec3 sub3(const Vec3& a, const Vec3& b) {
    return {a[0] - b[0], a[1] - b[1], a[2] - b[2]};
}

} // namespace ps::rotation
